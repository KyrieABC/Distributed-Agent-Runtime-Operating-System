#include "dar/runtime/task_manager.h"

#include <utility>

namespace dar
{
    // if the state belongs to any terminal state
    bool TaskManager::IsTerminal(ExecutionState state) noexcept
    {
        return state==ExecutionState::kSSucceeded || state == ExecutionState::kFailed || state == ExecutionState::kCancelled;
    }

    /**
     * TransitionLocked (Lifecycle gatekeeper)
     * 
     * Every ordinary state transition passes through this function
     */
    Status TaskManager::TransitionLocked(Record& record, ExecutionState next_state)
    {
        const ExecutionState current = record.state;

        // Whether the current state can transfer to the next_state (if current==terminal then auto false)
        bool valid = false;

        switch(current)
        {
            case ExecutionState::kPending:
              valid = next_state == ExecutionState::kQueued;
              break;
            case ExecutionState::kQueued:
              valid = next_state == ExecutionState::kScheduled || next_state == ExecutionState::kCancelled;
              break;
            case ExecutionState::kScheduled:
              valid = next_state == ExecutionState::kRunning;
              break;
            case ExecutionState::kRunning:
              valid = next_state == ExecutionState::kSSucceeded || next_state == ExecutionState::kFailed || next_state == ExecutionState::kCancelled;
              break;
            case ExecutionState::kSSucceeded:
            case ExecutionState::kFailed:
            case ExecutionState::kCancelled:
              valid = false;
              break;
        }
        if(!valid)
        {
            return Status::FailedPrecondition("illegal task lifecycle transition");
        }
        record.state = next_state;
        return Status::OK();
    }

    // Register
    Status TaskManager::Register(TaskSpec spec, TaskHandler handler)
    {
        // Creates a scoped lock that uses RAII for automatic locking and unlocking of mutex
        /**
         * When lock is initialized, constructor call mu_.lock(), blocking current thread until it successfully acquires ownership of mu_
         * When lock goes out of scope, its(mu_) destrucor automatically call mu_.unlock()
         * -> Because unlocking happens in destructor, guaranteed to be unlock even if unexpected exception occured inside function, Prevents deadlock
         */
        std::lock_guard<std::mutex> lock(mu_);

        // TaskID identifies the LOGICAL task
        // Register it twice would create two competing authoritative records
        /**
         * .find(id) -> returns a iterator to the element matching
         */
        if(records_.find(spec.id)!=records_.end())
        {
            return Status::AlreadyExists("Task already registered");
        }

        if(!handler)
        {
            return Status::InvalidArgument("task handler must not be empty");
        }

        auto record = std::make_unique<Record>();

        record->spec = std::move(spec);
        record->handler = std::move(handler);

        // For clarity: Logical task exists but not yet queed or attempted
        record->state = ExecutionState::kPending;
        record->attempt = 0;

        const TaskID id = record->spec.id;

        // unordered map emplace: Insert (only if unique) a new key-value pair into map but construct the element in-place
        records_.emplace(id,std::move(record));

        return Status::OK();
    }

    Status TaskManager::MarkQueued(TaskID id)
    {
        std::lock_guard<std::mutex> lock(mu_);

        const auto it = records_.find(id);

        if(it == records_.end())
        {
            return Status::NotFound("task is not registered");
        }

        // Whether it is able to (return a OK() status or failPrecondition() status)
        return TransitionLocked(*it->second, ExecutionState::kQueued);
    }

    Status TaskManager::MarkScheduled(TaskID id, WorkerID worker_id, ExecutionID execution_id)
    {
        std::lock_guard<std::mutex> lock(mu_);

        const auto it = records_.find(id);
        if(it == records_.end())
        {
            return Status::NotFound("Task is not registered");
        }

        Record& record = *it->second;

        // Validate tarnsition before mutating attempt metatdata
        // Otherwise illegal MarkScheduled() call could increment atttempt even though lifecycle transitoin failed
        Status status = TransitionLocked(record, ExecutionState::kScheduled);

        if(!status.ok())
        {
            return status;
        }

        /**
         * Increment attempt in MarkScheduled instead of MarkRunning:
         *  Scheduler choose: WorkerID, ExecutionID
         *  - Now a concrete attempt exists: TaskID -> Logical Task(attempt = 1, ExecutionID(x), WorkerID(w))
         */
        ++record.attempt;

        record.worker = worker_id;
        record.execution_id = execution_id;

        return Status::OK();
    }

    Status TaskManager::MarkRunning(TaskID id)
    {
        std::lock_guard<std::mutex> lock(mu_);

        const auto it = records_.find(id);
        if(it == records_.end())
        {
            return Status::NotFound("task is not registered");
        }

        return TransitionLocked(*it->second, ExecutionState::kRunning);
    }

    // Complete a running execution
    // ?
    Status TaskManager::Complete(TaskID id, ExecutionOutcome outcome)
    {
        std::lock_guard<std::mutex> lock(mu_);

        const auto it = records_.find(id);
        if(it==records_.end())
        {
            return Status::NotFound("task is not registered");
        }

        Record& record = *it->second;

        ExecutionState terminal_state;

        if(outcome.status.ok())
        {
            terminal_state = ExecutionState::kSSucceeded;
        }
        else if(outcome.status.IsCancelled())
        {
            terminal_state = ExecutionState::kCancelled;
        }
        else
        {
            terminal_state = ExecutionState::kFailed;
        }

        // Only RUNNING tasks may complete (is it okay to go to the terminal_stae determined by )
        Status transition = TransitionLocked(record, terminal_state);

        if(!transition.ok())
        {
            return transition;
        }

        record.terminal_status = std::move(outcome.status);
        record.result = std::move(outcome.result);

        // Wake every GetResult() waiter
        record.cv.notify_all();

        return Status::OK();
    }

    Status TaskManager::Cancel(TaskID id)
    {
        std::lock_guard<std::mutex> lock(mu_);

        const auto it = records_.find(id);
        if(it == records_.end())
        {
            return Status::NotFound("task is not registered");
        }

        Record& record = *it->second;

        /**
         * Queued -> no code is executing yet
         * Therefore cancellation can become terminal immediately
         */
        if(record.state==ExecutionState::kQueued)
        {
            Status transition = TransitionLocked(record, ExecutionState::kCancelled);
            if(!transition.ok())
            {
                return transition;
            }
            record.cancel_token->store(true, std::memory_order_release);
            record.terminal_status=Status::Cancelled("task cancelled before execution");
            record.result.clear();
            record.cv.notify_all();
            
            return Status::OK();
        }
        
        //Running
        /**
         * Don't do: record.state = KCancelled (Handler is still executing)
         * Instead:
         *   Running - (cancellation requested) -> Running
         *   RuntimeContext sees the token.
         *   Eventually the handler returns Status::Cancelled(), after which complete() perform (running -> cancelled)
         */
        if(record.state==ExecutionState::kRunning)
        {
            record.cancel_token->store(true,std::memory_order_release);
            return Status::OK();
        }

        // Cancellation afer completion doesn't rewrite history
        if(IsTerminal(record.state))
        {
            return Status::FailedPrecondition("task is already terminal");
        }

        // Currently do not cancel task in Scheduled-but-not-yet-running handoff window
        return Status::FailedPrecondition("task cannot be cancelled in its current state");
    }

    // Store the result to output(as using pointer, so it could be accessed outside)
    /**
     * TaskSnapshot  A;
     * Status a = GetSnapshot(id,&A) (changes made in A in function also effect A)
     */
    Status TaskManager::GetSnapshot(TaskID id, TaskSnapshot* out) const
    {
        // Why in the beginning?
        if(out==nullptr)
        {
            return Status::InvalidArgument("snapshot output must not be null");
        }

        std::lock_guard<std::mutex> lock(mu_);

        const auto it = records_.find(id);
        if(it==records_.end())
        {
            return Status::NotFound("task is not registered");
        }

        const Record& record = *it->second;

        out->id = record.spec.id;
        out->state = record.state;
        out->worker = record.worker;
        out->attempt = record.attempt;
        out->execution_id = record.execution_id;

        return Status::OK();
    }

    Status TaskManager::GetSpec(TaskID id, TaskSpec* out) const
    {
        if(out==nullptr)
        {
            return Status::InvalidArgument("TaskSpec output must not be null");
        }
        std::lock_guard<std::mutex> lock(mu_);
        const auto it = records_.find(id);
        if(it==records_.end())
        {
            return Status::NotFound("task is not registered");
        }

        // is *it or it here?
        *out = it->second->spec;
        return Status::OK();
    }

    Status TaskManager::GetHandler(TaskID id, TaskHandler* out) const
    {
        if(out==nullptr)
        {
            return Status::InvalidArgument("TaskHandler output must not be null");
        }

        std::lock_guard<std::mutex> lock(mu_);

        const auto it = records_.find(id);

        if(it == records_.end())
        {
            return Status::NotFound("task is not registered");
        }

        *out = it->second->handler;

        return Status::OK();
    }

    Status TaskManager::GetCancellationToken(TaskID id, std::shared_ptr<std::atomic_bool>* out) const
    {
        if(out==nullptr)
        {
            return Status::InvalidArgument("cancellation token output must not be null");
        }

        std::lock_guard<std::mutex> lock(mu_);

        const auto it = records_.find(id);
        if(it==records_.end())
        {
            return Status::NotFound("task is not registered");
        }

        *out = it->second->cancel_token;

        return Status::OK();
    }

    Status TaskManager::GetResult(TaskID id, std::string* out, std::chrono::milliseconds timeout) const 
    {
        if(out==nullptr)
        {
            return Status::InvalidArgument("result output must be null");
        }

        // condition_variable requires a lock that can be temporarily released while waiting
        std::unique_lock<std::mutex> lock(mu_);

        const auto it = records_.find(id);
        if(it==records_.end())
        {
            return Status::NotFound("task is not registered");
        }

        Record& record = *it->second;

        const auto terminal = [&record](){ return IsTerminal(record.state);};
        
        if(timeout == std::chrono::milliseconds::max())
        {
            record.cv.wait(lock, terminal);
        }
        else
        {
            const bool completed = record.cv.wait_for(lock, timeout, terminal);
            if(!completed)
            {
                return Status::DeadlineExceeded("timed out waiting for task result");
            }
        }

        // At this point, the state must be terminal
        // Only successful execution produces a successful GetResult()
        if(!record.terminal_status.ok())
        {
            return record.terminal_status;
        }

        *out = record.result;
        return Status::OK();
    }
}


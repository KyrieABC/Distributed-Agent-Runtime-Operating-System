/**
 * Central Transaction:
 * No mutation 
 * -> Peek FIFO head 
 * -> GetSpec
 * -> IdleWorkers
 * -> first-fit
 * -> TryReserve
 * -> worker owned
 * -> Acquire
 * -> worker + resources owned
 * -> Get handler + token
 * -> Pop
 * -> new ExecutionID
 * -> MarkScheduled
 * -> DispatchReserved
 * -> Worker
 * -> MarkRunning
 * -> Execute(...)
 * -> Complete()
 * -> IDLE
 * -> OnWorkerIdle()
 * -> Release(resources)
 * -> wake Scheduler
 */
/**
 * Explicitly does strict FIFO preservation
 * Worker: A, B, C
 * Scheduler: Peek() -> A, SelectWorker(A)-> none. Then STOP
 * 
 * does not: A can't run -> try B (NOO)
 */
/**
 * For phase 1: Cannot rollback(to previous state) after
 *  -> after task_queue_.Pop(...) and task_manager_.MarkScheduled(...)
 *    without TaskQueue::PushFront(...) and TaskManager::RolbackScheduled(...)
 * 
 * So, you can:
 * 1. Reserve succeeds, Acquire fails (ReleaseReservation())
 * 2. Reserve succeeds, Acquire fails, Pop fails (Release(resources),ReleaseReservation())
 * 
 * But not: 
 * Pop succeeds, Mark Scheduld fails
 * or
 * MarkScheduled succeeds, DispatchReserved fails (lifecycle prohibits Scheduled -> Queued)
 */
#include "dar/scheduler/scheduler.h"

#include <memory>
#include <utility>
#include <vector>

namespace dar
{
    Scheduler::Scheduler(
        TaskQueue& task_queue,
        TaskManager& task_manager,
        PlacementPolicy& placement,
        ResourceManager& resource_manager,
        WorkerPool& worker_pool
    ) : task_queue_(task_queue), task_manager_(task_manager), placement_(placement), resource_manager_(resource_manager), worker_pool_(worker_pool)
    {
        // Worker completion is an event relevant to scheduling:
        /**
         * Busy -> IDLE
         * At that point:
         *   1. Logical resources must be returned
         *   2. Queued work may now become runnable
         */
        worker_pool_.SetIdleCallback(
            [this](WorkerID worker_id){OnWorkerIdle(worker_id);}
        );
    }

    Scheduler::~Scheduler()
    {
        (void)Shutdown();
    }

    Status Scheduler::Start()
    {
        std::lock_guard<std::mutex> lock(mu_);
        if(started_)
        {
            return Status::OK();
        }

        stopping_ = false;
        wake_requested_ = true;
        started_ = true;

        // The Run() function
        threads_ = std::thread(&Scheduler::Run, this);
        return Status::OK(); 
    }

    /**
     * Runtime requires:
     * Stop submission 
     * -> Finish accepted queued + running work
     * -> stop scheduler 
     * -> stop workers
     */
    Status Scheduler::Shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(mu_);

            if(!started_)
            {
                return Status::OK();
            }

            stopping_ = true;
            wake_requested_ = true;
        }
        cv_.notify_one();

        // Never join while holding mu_
        // Run() needs mu_ to observe stopping_
        if(threads_.joinable())
        {
            threads_.join();
        }

        {
            std::lock_guard<std::mutex> lock(mu_);
            started_ = false;
        }
        return Status::OK();
    }

    Status Scheduler::Enqueue(TaskID task_id)
    {
        // Serialize queue/lifecycle chagnes that belong to scheduler
        // particularly useful for Enqueue() vs Cancel()
        std::lock_guard<std::mutex> lock(mu_);

        if(!started_||stopping_)
        {
            return Status::Unavailable("Scheduler is not accepting tasks");
        }
        // Pending -> Queued
        Status queued_status = task_manager_.MarkQueued(task_id);
        if(!queued_status.ok())
        {
            // return the state from MarkQueued()
            return queued_status;
        }

        // Append to FIFO
        Status push_status  = task_queue_.Push(task_id);

        if(!push_status.ok())
        {
            /**
             * Current TaskManager has no: Queued -> PENDING rollback operation
             * This would represent an internal invariant failure
             * 
             * Currently, TaskQueue Push() returns OK after push_back, 
             * This path should NOT occur under normal operation
             */
            return push_status;
        }

        wake_requested_ = true;
        cv_.notify_one();
        return Status::OK();
    }

    Status Scheduler::Cancel(TaskID task_id)
    {
        std::lock_guard<std::mutex> lock(mu_);

        TaskSnapshot snapshot;

        Status snapshot_status = task_manager_.GetSnapshot(task_id, &snapshot);
        if(!snapshot_status.ok())
        {
            return snapshot_status;
        }

        // Queued cancellation
        /**
         * remove it form the physical scheduling queue FIRST
         * Then: Queued->Cancelled
         * -> Prevent a terminal cancelled task from remianing as the FIFO head
         */
        if(snapshot.state==ExecutionState::kQueued)
        {
            Status remove_status = task_queue_.Remove(task_id);
            if(!remove_status.ok())
            {
                return remove_status;
            }

            Status cancel_status = task_manager_.Cancel(task_id);

            if(!cancel_status.ok())
            {
                // Queue removal succeeded but lifecycle cancellation failed
                // Current TaskQueue has no PushFront(), so exact FIFO rollback is unavailable
                // Under Scheduler serialization this should indicate an invariant Violation rather than an expected runtime conditon
                return cancel_status;
            }

            // Remove the head may unblock the next FIFO task
            wake_requested_ = true;
            cv_.notify_one();

            return Status::OK();
        }

        // Running Cancellation
        /**
         * TaskManager keeps state RUNNING and sets the cancellation token
         * 
         * RuntimeContext observes that token
         */
        return task_manager_.Cancel(task_id);
    }

    void Scheduler::Run()
    {
        for(;;)
        {
            // Sleep until something potentially change
            {
                std::unique_lock<std::mutex> lock(mu_);

                cv_.wait(
                    lock,
                    [this](){return stopping_||wake_requested_;}
                );

                if(stopping_){break;}

                wake_requested_ = false;
            }
            
            // Keep dispatching while immediate progress is possible
            /**
             * Ex: 4 idle workers, 4 CPU task queued
             * One wakeup should be sufficient to dispatch all four rather than dispatching one and going immediately back to sleep
             */
            for(;;)
            {
                {
                    std::lock_guard<std::mutex> lock(mu_);

                    if(stopping_)
                    {
                        return;
                    }
                }
                bool dispatched = false;

                Status status = TryScheduleOne(&dispatched);

                if(!status.ok())
                {
                    // Avoid spinning on an invariant failure
                    break;
                }

                if(!dispatched)
                {
                    /**
                     * Either:
                     * queue empty or FIFO head cannot run right now
                     * 
                     * In second case, intentially do NOT inspect task #2
                     */
                    break;
                }
            }
        }
    }

    Status Scheduler::TryScheduleOne(bool* dispatched)
    {
        if(dispatched==nullptr)
        {
            return Status::InvalidArgument("dispatched output must not be null");
        }

        *dispatched = false;

        // Serialize the scheduling transaction against Enqueue() and Cancel()
        std::lock_guard<std::mutex> lock(mu_);

        if(stopping_)
        {
            return Status::OK();
        }

        // Step 1: Peek FIFO head
        // ? no definition?
        TaskID task_id;
        Status peek_status = task_queue_.Peek(&task_id);
        if(!peek_status.ok())
        {
            // empty queue is normal
            return Status::OK();
        }

        // Step 2: Get TaskSpec
        TaskSpec spec;
        Status spec_status = task_manager_.GetSpec(task_id,&spec);

        if(!spec_status.ok())
        {
            return spec_status;
        }

        // Step 3: Observe currently idle workers
        const std::vector<WorkerID> idle_workers = worker_pool_.IdleWorkers();

        if(idle_workers.empty())
        {
            return Status::OK();
        }

        // Step 4: First-fit placement
        // PlacementPolicy checks AVAILABLE resources (does NOT mutate anything)
        const std::optional<WorkerID> selected = placement_.SelectWorker(
            spec.resources, idle_workers,resource_manager_
        );

        if(!selected.has_value())
        {
            /**
             * Strict FIFO
             * Ex: 
             * A = GPU task, currently blocked
             * B = CPU task, runnable
             * C = CPU task, runnable
             * 
             * Queue remains: A,B,C
             * Wair for A
             */
            return Status::OK();
        }

        const WorkerID worker_id = *selected;
        // Step 5: Reserve exact worker
        /**
         * IDLE -> Reserved
         * 
         * placement only observed the worker
         * Reserve() atomically claims it
         */
        Status reserve_status = worker_pool_.TryReserve(worker_id);
        if(!reserve_status.ok())
        {
            // Worker automatically changed between observations and reservation
            // No resource/task state has changed yet
            return Status::OK();
        }

        // Step 6: Acquire logical resources
        Status acquire_status = resource_manager_.Acquire(
            worker_id, spec.resources
        );

        if(!acquire_status.ok())
        {
            // Roll back: Reserved -> IDLE
            (void)worker_pool_.ReleaseReservation(worker_id);
            return Status::OK();
        }

        // Owns: worker reservation, resource reservation
        // the task is stil queued
        // Fetch execution material BEFORE removing the task 
        // -> reduces the number of operations that can fail after Pop()
        TaskHandler handler;

        Status handler_status = task_manager_.GetHandler(
            task_id, &handler
        );

        if(!handler_status.ok())
        {
            (void)resource_manager_.Release(
                worker_id, spec.resources
            );

            (void)worker_pool_.ReleaseReservation(worker_id);

            return handler_status;
        }

        RuntimeContext::CancellationToken cancel_token;

        Status token_status = task_manager_.GetCancellationToken(task_id, &cancel_token);
        if(!token_status.ok())
        {
            (void)resource_manager_.Release(worker_id, spec.resources);
            (void)worker_pool_.ReleaseReservation(worker_id);

            return token_status;
        }

        // Step 7: Remove exact FIFO head
        TaskID popped_id;
        Status popped_status = task_queue_.Pop(&popped_id);

        if(!popped_status.ok())
        {
            (void)resource_manager_.Release(worker_id, spec.resources);
            (void)worker_pool_.ReleaseReservation(worker_id);

            return popped_status;
        }

        // Scheduler queue mutation is serialized under mu_, Pop() must return the same task observed by Peek()
        if(popped_id!=task_id)
        {
            // This indicates violation of scheduler's FIFO ownership assumption
            // Resource and worker reservation can be rolled back, but queue has already been mutated
            // Treat as an internal consistency error
            (void)resource_manager_.Release(worker_id,spec.resources);
            (void)worker_pool_.ReleaseReservation(worker_id);

            return Status::Internal("FIFO queue head changed during scheduling transaction");
        }

        // Step 8: Create concrete execution identity
        const ExecutionID execution_id = ExecutionID::Random();

        // Step 9: Queued -> Scheduled
        Status scheduled_status = task_manager_.MarkScheduled(
            task_id,
            worker_id,
            execution_id
        );

        if(!scheduled_status.ok())
        {
            // Workers/resources can be rolled back
            (void)resource_manager_.Release(worker_id,spec.resources);
            (void)worker_pool_.ReleaseReservation(worker_id);

            /**
             * Cannot perfectly restore FIFO because TaskQueue currently exposes Push() but not PushFront()
             * 
             * Under intended ownership model, Scheduler::mu_ prevents Scheduler::Cancel() from racing this transaction
             * -> so this failure should represent an invariant violation
             */
            return scheduled_status;
        }

        // Step 10: Record acquired resources
        // Worker completion callback uses this to return capacity
        running_.emplace(worker_id,RunningReservation{
            task_id,
            spec.resources
        });

        // Step 11: Construct WorkItem
        WorkItem work{
            spec, 
            execution_id,
            std::move(handler),
            std::move(cancel_token)
        };

        // Step 12: Reserved -> BUSY
        Status dispatch_status = worker_pool_.DispatchReserved(worker_id,std::move(work));

        if(!dispatch_status.ok())
        {
            running_.erase(worker_id);
            (void)resource_manager_.Release(worker_id,spec.resources);
            (void)worker_pool_.ReleaseReservation(worker_id);

            // No Scheduled -> Queued rollback in TaskManager
            // DispatchReserved() failing after successful reservatoin and MarkSchedueld() is an internal invariant failure 
            return dispatch_status;
        }

        *dispatched=true;
        return Status::OK();
    }

    void Scheduler::OnWorkerIdle(WorkerID worker_id)
    {
        ResourceRequest resources;
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(mu_);

            const auto it = running_.find(worker_id);

            if(it!=running_.end())
            {
                resources=it->second.resources;
                running_.erase(it);
                found=true;
            }
        }

        // Return logical resources
        // do NOT hold Scheduler::mu_ while entering ResourceManager
        if(found)
        {
            (void)resource_manager_.Release(worker_id,resources);
        }

        // Work SCheduler
        // A FIFO head that could not run before may now fit
        {
            std::lock_guard<std::mutex> lock(mu_);
            if(started_&&!stopping_)
            {
                wake_requested_=true;
            }
        }

        // notify both SCheduler::Run() and Scheduler::Drain()
        cv_.notify_all();
    }

    Status Scheduler::Drain()
    {
        std::unique_lock<std::mutex> lock(mu_);

        if(!started_)
        {
            return Status::FailedPrecondition("scheduler is not started");
        }

        cv_.wait(
            lock,
            [this]()
                {
                    // A task is completedly drained from Scheduler when:
                    // 1. NO task remains waiting in TaskQueue
                    // 2. NO worker still owns scheduler-acquired resources
                    return task_queue_.Empty()&&running_.empty();
                });
        return Status::OK();
    }
}
#include "dar/worker/worker.h"

#include <utility>

namespace dar
{
    Worker::Worker(WorkerID id, TaskManager& task_manager, IdleCallback idle_callback)
    : id_(id), task_manager_(task_manager),idle_callback_(idle_callback){}

    Worker::~Worker()
    {
        // A joinable std::thread must not reach its destructor
        // Shutdown() signals Run() to exit and joins the underlying thread
        (void)Shutdown();
    }

    Status Worker::Start()
    {
        std::lock_guard<std::mutex> lock(mu_);

        if(started_)
        {
            return Status::OK();
        }

        stopping_ = false;
        // ?
        thread_ = std::thread(&Worker::Run,this);

        started_ = true;

        return Status::OK();
    }

    Status Worker::Shutdown()
    {
        // lock is released before join()
        {
            std::lock_guard<std::mutex> lock(mu_);

            if(!started_)
            {
                return Status::OK();
            }

            // Request worker-loop termination
            // Do not forcibly terminate a running handler
            // IF state_ == BUSY, Run() finish that WorkItem first and observe stopping_ on the next loop iteration
            stopping_ = true;

        }

        // Wake Run() if it is currently sleeping while IDLE
        cv_.notify_one();

        // Never hold mu_ while joining
        // Run() needs mu_ in order to observe stopping_ and exit
        if(thread_.joinable())
        {
            thread_.join();
        }

        {
            std::lock_guard<std::mutex> lock(mu_);

            started_ = false;
            stopping_ = false;

            // Once execution thread is gone, there must be outstanding mailbox work
            mailbox_.reset();

            state_ = WorkerState::kIdle;
        }

        return Status::OK();
    }

    WorkerState Worker::state() const
    {
        std::lock_guard<std::mutex> lock(mu_);
        return state_;
    }

    bool Worker::IsIdle() const
    {
        std::lock_guard<std::mutex> lock(mu_);
        return state_==WorkerState::kIdle;
    }

    /**
     * Scheduler A: Reserve() -> lock(mu_) -> IDLE? yes -> (IDLE -> RESERVED)
     * Scheduler B: Reserve() -> lock(mu_) -> IDLE? No ->reject
     */
    Status Worker::Reserve()
    {
        std::lock_guard<std::mutex> lock(mu_);

        if(!started_||stopping_)
        {
            return Status::Unavailable("Worker is not available");
        }

        /**
         * This check + transition occur under ONE mutex acquisition
         * Two scheduler threads cannot both observe IDLE and successfully reserve this worker
         */
        if(state_!=WorkerState::kIdle)
        {
            return Status::FailedPrecondition("worker is not idle");
        }

        state_ = WorkerState::kReserved;

        return Status::OK();
    }

    Status Worker::Dispatch(WorkItem work)
    {
        std::lock_guard<std::mutex> lock(mu_);

        if(!started_ || stopping_)
        {
            return Status::Unavailable("Worker is not accepting work");
        }

        /**
         * Scheduler must reserve before dispatch
         * Enforce:
         * IDLE - Reserve() -> Reserved - Dispatch() -> BUSY
         */
        if(state_!=WorkerState::kReserved)
        {
            return Status::FailedPrecondition("worker must be reserved before dispatch");
        }

        // Impossile if worker invariants are respected
        if(mailbox_.has_value())
        {
            return Status::FailedPrecondition("worker mailbox already contains work");
        }

        mailbox_.emplace(std::move(work));

        state_ = WorkerState::kBusy;
        
        //Notify after releasing mu_
        // Run() will wake, reacquire mu_, and take the WorkItem
        cv_.notify_one();
        
        return Status::OK();
    }

    Status Worker::ReleaseReservation()
    {
        std::lock_guard<std::mutex> lock(mu_);

        if(!started_ || stopping_)
        {
            return Status::Unavailable("Worker is not available");
        }

        if(state_ != WorkerState::kReserved)
        {
            return Status::FailedPrecondition("worker is not reserved");
        }

        // No workitem should exist before Dispatch()
        if(mailbox_.has_value())
        {
            return Status::FailedPrecondition("reserved worker unexpectedly contains work");
        }

        state_ = WorkerState::kIdle;
        return Status::OK();
    }

    void Worker::Run()
    {
        for(;;)
        {
            std::optional<WorkItem> work;

            // Wait for work
            {
                std::unique_lock<std::mutex> lock(mu_);

                /**
                 * Predicate-based waiting handles spurious wakeups
                 * 
                 * Wake when:
                 *   1. Shutdown() has been requested
                 *   2. Dispatch() placed something in mailbox
                 */
                cv_.wait(lock,[this](){
                    return stopping_ || mailbox_.has_value();
                });

                // If shutdown was requested while there is no outstanding work, terminate the worker thread
                // If mailbox contains work, we intentionally execute it before shutting it down.
                // -> Gives Worker::Shutdown() drain semantics
                if(stopping_&&!mailbox_.has_value())
                {
                    break;
                }

                // Move the WorkItem out of the shared mailbox
                // Handler execution must NEVER happen while holding mu_
                work.emplace(std::move(*mailbox_));
                mailbox_.reset();
            }

            /**
             * Construct execution context
             * One RuntimeContext corresponds to one concrete execution attempt
             */
            RuntimeContext context(work->spec.id, work->execution_id, id_, work->cancel_token);

            /**
             * Scheduled -> Running
             * 
             * TaskManager owns lifecycle correctness
             * Worker requests the transition; does NOT modify state directly
             */
            Status running_status = task_manager_.MarkRunning(work->spec.id);

            if(running_status.ok())
            {
                /**
                 * Execute exactly one task
                 * Executor:
                 * TaskSpec -> RuntimeContext -> TaskHandler -> ExecutionOutcome
                 */
                ExecutionOutcome outcome = executor_.Execute(work->spec, context, work->handler);

                /**
                 * Running -> terminal
                 * Complete() decides:
                 *   OK -> SUCCEEDED
                 *   Cancelled -> CANCELLED
                 *   ERROR -> FAILED
                 */
                (void)task_manager_.Complete(work->spec.id, std::move(outcome));
            }

            // Return Worker to IDLE
            {
                std::lock_guard<std::mutex> lock(mu_);
                state_ = WorkerState::kIdle;
            }

            /**
             * Notify owner
             * 
             * Callback runs WITHOUT mu_ held
             * Otherwise callback code(worker.IsIdle()) would try to reacquire mu_ and deadlock
             */
            if(idle_callback_)
            {
                idle_callback_(id_);
            }
        }
    }
}
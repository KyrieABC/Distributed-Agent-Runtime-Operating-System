/**
 * Runtime:
 * 1. TaskManager -> WorkerPool (Worker1, Worker2, ...) 
 * 2. ResourceManager 
 * 3. TaskQueue
 * Scheduler -> 1,2,3
 * Scheduler -> PlacementPolicy
 */


#include "dar/runtime/runtime.h"

#include <utility>
#include <vector>

namespace dar
{
    // The explanation of how without StrongHashID on task_manager's unordered map records_ could make "TaskManager() = default" implicitly deleted
    // task_manager_ cannot use the () for initiailzer list
    Runtime::Runtime(RuntimeOptions options)
    : options_(std::move(options)), 
    resource_manager_(),
    task_manager_(),
    worker_pool_(options_.worker_count,task_manager_),
    task_queue_(),
    placement_(),
    scheduler_(
        task_queue_,
        task_manager_,
        placement_,
        resource_manager_,
        worker_pool_
    ){}

    Runtime::~Runtime()
    {
        // Runtime Owns thread-owning componenets
        // Ensure they are shut down before Runtime's members are destoryed
        // Follows same ownership Principle as RAII: owner of a resource is responisble for releasing it
        (void)Shutdown();
    }

    // Start
    Status Runtime::Start()
    {
        std::lock_guard<std::mutex> lock(lifecycle_mu_);

        if(started_)
        {
            return Status::OK();
        }

        /**
         * Step 1: 
         * Register logical resources for every Worker
         * 
         * WorkerPool owns worker identity
         * ResourceManager owns logical capacity
         * Runtime wires those two worlds together
         */
        if(!resources_initialized_)
        {
            const std::vector<WorkerID> worker_ids = worker_pool_.WorkerIDs();

            for(const WorkerID& worker_id : worker_ids)
            {
                Status status = resource_manager_.RegisterWorker(
                    worker_id, options_.resources_per_worker
                );

                if(status.ok())
                {
                    return status;
                }
            }
            resources_initialized_= true;
        }

        // Step 2: Start execution workers
        Status worker_status = worker_pool_.Start();
        if(!worker_status.ok())
        {
            return worker_status;
        }

        // Step 3: Start scheduling thread
        Status scheduler_status = scheduler_.Start();

        if(!scheduler_status.ok())
        {
            // Scheduler failed to start after workers started
            // Roll back the dependency that we already started
            (void)worker_pool_.Shutdown();
            return scheduler_status;
        }

        // Step 4: Open admission only after the whole runtime is operational
        started_ = true;
        accepting_ = true;

        return Status::OK();
    }

    Status Runtime::Submit(TaskSpec spec, TaskHandler handler)
    {
        std::lock_guard<std::mutex> lock(lifecycle_mu_);

        // Admission gate
        if(!started_||!accepting_)
        {
            return Status::Unavailable("runtime is not accepting tasks");
        }

        // Validate the task itself before storing it
        Status validation_status = spec.Validate();

        // Why don't release the resources here?
        if(!validation_status.ok())
        {
            return validation_status;
        }

        if(!handler)
        {
            return Status::InvalidArgument("task handler must not be empty");
        }

        /**
         * Feasibility admission control
         * 
         * check TOTAL capacity, not Available Capacity
         * -> RUntime should Accept and allow scheduler to wait (if <total but >currentAvailable)
         */
        bool feasible = false;

        const std::vector<WorkerID> worker_ids = worker_pool_.WorkerIDs();
        for(const WorkerID& worker_id : worker_ids)
        {
            if(resource_manager_.CanFitTotal(worker_id, spec.resources))
            {
                feasible = true;
                break;
            }
        }

        if(!feasible)
        {
            return Status::ResourceExhausted("task cannot fit on any worker's total resource capacity");
        }

        // Save ID before moving spec into TaskManager
        const TaskID task_id = spec.id;

        // Register logical task
        // Initial Lifecycle: pending
        Status register_status = task_manager_.Register(
            std::move(spec),
            std::move(handler)
        );

        if(!register_status.ok())
        {
            return register_status;
        }

        // Queue task
        /**
         * Scheduler::Enqueue() ALREADY performs: TaskManager::MarkQueue(task_id)
         *  -> followed by : TaskQueue::Push(task_id)
         * 
         * Runtime must NOT call MarkQueue() itself
         */
        Status enqueue_Status = scheduler_.Enqueue(task_id);

        if(!enqueue_Status.ok())
        {
            /**
             * Task is already registered
             * 
             * Current TaskManager has no Unregister(), no complete registration rollback API
             * 
             * Under normal operation, Scheduler::Enqueue() should succeed once Runtime admission is open
             */
            return enqueue_Status;
        }

        return Status::OK();
    }

    Status Runtime::Cancel(TaskID id)
    {
        // Do not call TaskManager::Cancel() here first
        /**
         * Scheduler::Cancel() already coordinates:
         *   QUEUED: TaskQueue::Remove(), TaskManager::Cancel()
         *   RUNNING: TaskManager::Cancel()
         * 
         * Calling TaskManager::Cancel() first would leave a cancelled QUEUED TaskID physically inside TaskQueue
         * Use scheduler::cancel() instead so it removes the task_id in the queue while call the TaskManager::Cancel()
         */
        return scheduler_.Cancel(id);
    }

    Status Runtime::GetStatus(TaskID id, TaskSnapshot* out) const
    {
        return task_manager_.GetSnapshot(id, out);
    }

    Status Runtime::GetResult(TaskID id, std::string* out, std::chrono::milliseconds timeout) const
    {
        return task_manager_.GetResult(id, out, timeout);
    }

    Status Runtime::Shutdown()
    {
        // Step 1: close admission
        // Do this under Runtime's lifecycle mutex
        {
            std::lock_guard<std::mutex> lock(lifecycle_mu_);

            if(!started_)
            {
                return Status::OK();
            }

            accepting_ = false;
        }

        /**
         * DO NOT hold lifecycle_mu_ while draining
         * 
         * Drain() may wait for:
         *   queued tasks
         *   running handlers
         *   worker callbacks
         * 
         * Holding an unrelated high-level lifecycle mutex during a blocking operation would unnecessarily enlarge the critical section
         */

        // Step 2: Drain all work accepted before admission closed
        Status drain_status = scheduler_.Drain();

        if(!drain_status.ok())
        {
            return drain_status;
        }

        /**
         * Now: TaskQueue : empty
         * And: Scheduler;:running_ : empty
         * 
         * All accepted work has reached completion/cancellation
         */

         // Step 3: Stop scheduler thread
        Status scheduler_status = scheduler_.Shutdown();

        if(!scheduler_status.ok())
        {
            return scheduler_status;
        }

        // Step 4: Stop worker threads
        // Scheduler can no longer dispatch new work
        Status worker_status = worker_pool_.Shutdown();
        
        if(!worker_status.ok())
        {
            return worker_status;
        }

        // Step 5: Publish stopped state
        {
            std::lock_guard<std::mutex> lock(lifecycle_mu_);

            started_ = false;
            accepting_ = false;
        }

        return Status::OK();
    }
}
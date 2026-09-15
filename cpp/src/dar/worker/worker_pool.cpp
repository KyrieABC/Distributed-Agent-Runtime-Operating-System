#include "dar/worker/worker_pool.h"

#include <utility>

namespace dar
{
    WorkerPool::WorkerPool(std::size_t worker_count, TaskManager& task_manager): task_manager_(task_manager)
    {
        // std::vector reserve (pre-allocate memory for a vector without creating any actual element)
        // Used for size
        // Modify vector's capacity()(amount of memory allocated) while leaving its size()(number of elements currently in use exactly the same)
        worker_ids_.reserve(worker_count);
        // sets number of buckets in has table at least (parameter) elements without triggering a rehash
        workers_.reserve(worker_count);

        for(std::size_t i =0;i<worker_count;++i)
        {
            WorkerID worker_id = WorkerID::Random();
            
            // Defensive uniqueness check
            // Random 128-bit collision(same) are unlikely 
            // Need to make sure: 1 WorkerID -> 1 worker
            while(workers_.find(worker_id)!=workers_.end())
            {
                // Continue in loop until find a unique ID not same with any others
                worker_id = WorkerID::Random();
            }

            worker_ids_.push_back(worker_id);

            auto worker = std::make_unique<Worker>(
                worker_id,
                task_manager_,
                [this](WorkerID id){OnWorkerIdle(id);});
            workers_.emplace(worker_id, std::move(worker));
        }
    }

    WorkerPool::~WorkerPool()
    {
        // Explicit discard function's return value
        // Some function marked with [[nodiscard]] (call without result compiler will throw warning)
        (void)Shutdown();
    }

    Status WorkerPool::Start()
    {
        std::lock_guard<std::mutex> lock(mu_);

        if(started_)
        {
            return Status::OK();
        }

        for(const WorkerID& worker_id : worker_ids_)
        {
            Worker* worker = FindWorkerLocked(worker_id);

            if(worker == nullptr)
            {
                // iterate from the vector but cannot find it
                return Status::Internal("worker pool contains inconsistent worker registry");
            }

            Status status = worker->Start();
            if(!status.ok())
            {
                // More advanced implementation could roll back worker already started here
                return status;
            }
        }
        
        started_ = true;
        return Status::OK();
    }

    Status WorkerPool::Shutdown()
    {
        /**
         Do not hold WorkerPool::mu_ while waiting for worker thread
         * Worker::Shutdown() may wait for a handler to finish
         * Worker then calls: OnWorkIdle() (need WorkerPool::mu_)
         * 
         * Holding he pool mutex while joining could create:
         * pool holds mu_ -> waits for Worker
         * Worker finishes -> OnWorkerIdle() -> waits for pool mu_
         * Deadlock
         */

        std::vector<Worker*> workers_to_shutdown;
        {
            std::lock_guard<std::mutex> lock(mu_);

            if(!started_)
            {
                return Status::OK();
            }

            workers_to_shutdown.reserve(worker_ids_.size());

            for(const WorkerID& worker_id: worker_ids_)
            {
                Worker* worker = FindWorkerLocked(worker_id);

                if(worker==nullptr)
                {
                    return Status::Internal("Worker pool contains inconsistent worker registry");
                }

                workers_to_shutdown.push_back(worker);
            }
            started_=false;
        }

        // No pool mutex here
        // Worker::Shutdown() may block until its worker thread finishes
        for(Worker* worker:workers_to_shutdown)
        {
            Status status = worker->Shutdown();
            if(!status.ok())
            {
                return status;
            }
        }
        return Status::OK();
    }

    std::vector<WorkerID> WorkerPool::WorkerIDs() const
    {
        std::lock_guard<std::mutex> lock(mu_);

        // Copy intentionally, caller cannot mutate WorkerPool's authoritative ordering
        return worker_ids_;
    }

    std::vector<WorkerID> WorkerPool::IdleWorkers() const
    {
        std::vector<WorkerID> idle;
        std::lock_guard<std::mutex> lock(mu_);

        idle.reserve(worker_ids_.size());

        for(const WorkerID& worker_id : worker_ids_)
        {
            const Worker* worker = FindWorkerLocked(worker_id);
            if(worker!=nullptr && worker->IsIdle())
            {
                idle.push_back(worker_id);
            }
        }
        return idle;
    }

    Status WorkerPool::TryReserve(WorkerID worker_id)
    {
        std::lock_guard<std::mutex> lock(mu_);

        if(!started_)
        {
            return Status::Unavailable("worker pool is not started");
        }

        Worker* worker = FindWorkerLocked(worker_id);

        if(worker==nullptr)
        {
            return Status::NotFound("Worker not found");
        }

        /**
         * Worker::Reserve() performs the actual atomic:
         *   check IDLE + transition to RESERVED
         * under worker's own mutex
         */
        // WorkerPool does NOT duplicate Worker state
        return worker->Reserve();
    }

    Status WorkerPool::ReleaseReservation(WorkerID worker_id)
    {
        std::lock_guard<std::mutex> lock(mu_);

        if(!started_)
        {
            return Status::Unavailable("worker pool is not started");
        }

        Worker* worker = FindWorkerLocked(worker_id);

        if(worker == nullptr)
        {
            return Status::NotFound("worker not found");
        }

        return worker->ReleaseReservation();
    }

    Status WorkerPool::DispatchReserved(WorkerID worker_id, WorkItem work)
    {
        std::lock_guard<std::mutex> lock(mu_);

        if(!started_)
        {
            return Status::Unavailable("Worker pool is not started");
        }

        Worker* worker = FindWorkerLocked(worker_id);

        if(worker==nullptr)
        {
            return Status::NotFound("Worker not found");
        }

        // Worker itself enforces: Reserved -> Busy
        return worker->Dispatch(std::move(work));
    }

    void WorkerPool::OnWorkerIdle(WorkerID worker_id)
    {
        IdleCallback callback;
        {
            std::lock_guard<std::mutex> lock(mu_);
            callback = idle_callback_;
        }

        // Never invoke arbitary callback code while holding WorkerPool::mu_
        if(callback)
        {
            callback(worker_id);
        }
    }

    Worker* WorkerPool::FindWorkerLocked(WorkerID worker_id)
    {
        auto it = workers_.find(worker_id);

        if(it == workers_.end())
        {
            return nullptr;
        }

        return it->second.get();
    }

    const Worker* WorkerPool::FindWorkerLocked(WorkerID worker_id) const
    {
        auto it = workers_.find(worker_id);

        if(it == workers_.end())
        {
            return nullptr;
        }

        return it->second.get();
    }
}

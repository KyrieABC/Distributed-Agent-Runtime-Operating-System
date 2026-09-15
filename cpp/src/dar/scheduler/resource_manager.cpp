#include "dar/scheduler/resource_manager.h"

#include <utility>

namespace dar
{
    Status ResourceManager::RegisterWorker(WorkerID worker_id, ResourceSet capacity)
    {
        // Creates a scoped lock that uses RAII for automatic locking and unlocking of mutex
        /**
         * When lock is initialized, constructor call mu_.lock(), blocking current thread until it successfully acquires ownership of mu_
         * When lock goes out of scope, its(mu_) destrucor automatically call mu_.unlock()
         * -> Because unlocking happens in destructor, guaranteed to be unlock even if unexpected exception occured inside function, Prevents deadlock
         */
        std::lock_guard<std::mutex> lock(mu_);

        // WorkerID must correspond to exactly 1 ResourcePool
        // Sliently replace 1 existing pool could destroy accounting for resources already acquired by running task
        if(pools_.find(worker_id)!=pools_.end())
        {
            return Status::AlreadyExists("worker already registered");
        }

        // ResourcePool(total) automatically initializes available resources itself
        pools_.emplace(worker_id,ResourcePool(std::move(capacity)));

        return Status::OK();
    }

    bool ResourceManager::CanFitAvailable(WorkerID worker_id, const ResourceRequest& request) const
    {
        std::lock_guard<std::mutex> lock(mu_);

        const auto it = pools_.find(worker_id);

        if(it==pools_.end())
        {
            return false;
        }

        // ResourcePool::CanFit() answer: "Can every requested resource be satisfied by Currently Available Logical resource?"
        return it->second.CanFit(request);
    }

    bool ResourceManager::CanFitTotal(WorkerID worker_id, const ResourceRequest& request) const
    {
        std::lock_guard<std::mutex> lock(mu_);

        const auto it = pools_.find(worker_id);

        if(it==pools_.end())
        {
            return false;
        }

        // CanFitTotal asks whether every requested quantity is <= worker's original total capacity
        const ResourceSet& total = it->second.Total();
        for(const auto& [name,requested] : request.resources().values())
        {
            // ResourceSet::Get() intentionally treats missing resources as 0
            /**
             * Ex: 
             * worker_total: {CPU: 4}
             * request:  {GPU:1}
             * 
             * total.get("GPU") == 0
             * return false 
             */
            if(total.Get(name)<requested)
            {
                return false;
            }
        }
        return true;
    }

    Status ResourceManager::Acquire(WorkerID worker_id, const ResourceRequest& request)
    {
        std::lock_guard<std::mutex> lock(mu_);

        const auto it = pools_.find(worker_id);
        if(it==pools_.end())
        {
            return Status::NotFound("Worker is not registered");
        }

        return it->second.Acquire(request);
    }

    Status ResourceManager::Release(WorkerID worker_id, const ResourceRequest& request)
    {
        std::lock_guard<std::mutex> lock(mu_);

        const auto it = pools_.find(worker_id);
        if(it==pools_.end())
        {
            return Status::NotFound("worker is not registered");
        }

        // ResourcePool::Release owns the accounting variant: available<=total
        return it->second.Release(request);
    }

    std::vector<WorkerID> ResourceManager::WorkerIDs() const
    {
        std::lock_guard<std::mutex> lock(mu_);

        std::vector<WorkerID> ids;
        ids.reserve(pools_.size());

        for(const auto& [worker_id,pool]: pools_)
        {
            ids.push_back(worker_id);
        }
        return ids;
    }

}
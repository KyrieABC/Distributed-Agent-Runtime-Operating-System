#pragma once

#include <mutex>
#include <unordered_map>
#include <vector>

#include "dar/common/status.h"
#include "dar/common/id.h"
#include "dar/core/resource.h"

namespace dar
{
    // ResourceManager owns the logical resource pools of all local workers
    // ResourceManager does NOT implement resource arithmetic itself
    // -> ResourcePool already owns the accounting rule for ONE worker
    /**
     * ResourcePool (per-worker accounting):
     *   - total
     *   - available 
     *   - CanFit()
     *   - Acquire()
     *   - Release()
     */
    /**
     * ResourceManager adds the worker dimension(worker->pool registry):
     *   WorkerID -> ResourcePool
     *   WorkerID -> ResourcePool
     *   WorkerID -> ResourcePool
     */
    class ResourceManager
    {
    public:
        // Register an already known WorkerID is rejected
        // Register one worker with its total logical capacity
        Status RegisterWorker(WorkerID worker_id, ResourceSet capacity);

        // Answers: could this task run on this worker?
        // Unlike CanFitAvailable(), this one checks the worker's total capacity
        // Runtime::Submit() uses this for admission control
        [[nodiscard]] bool CanFitTotal(WorkerID worker_id, const ResourceRequest& request) const;

        // Answers: Can this task run on this worker Right Now?
        [[nodiscard]] bool CanFitAvailable(WorkerID worker_id, const ResourceRequest& request) const;

        // Reserve resources from one worker
        Status Acquire(WorkerID worker_id, const ResourceRequest& request);

        // Return previously reserved resources
        // ResourcePool::Release() remain responsible for checking: available <= total
        Status Release(WorkerID worker_id, const ResourceRequest& request);

        // Return all workers known to the resource manager
        [[nodiscard]] std::vector<WorkerID> WorkerIDs() const;
    private:
        // ResourcePool already stores total and available capacity, no need for another WorkerResource struct
        // Use the given hash function (third argument)
        std::unordered_map<WorkerID, ResourcePool, StrongIDHash<WorkerID>> pools_;

        // Scheduler and runtime may access resource state concurrently
        // ResourcePool itself currently contains no mutex, so manager serializes access to all pools
        mutable std::mutex mu_;
    };
}
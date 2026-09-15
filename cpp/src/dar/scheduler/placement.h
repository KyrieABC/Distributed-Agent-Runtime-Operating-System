#pragma once

#include <optional>
#include <vector>

#include "dar/common/id.h"
#include "dar/core/resource.h"
#include "dar/scheduler/resource_manager.h"

namespace dar
{
    // Placement Policy -> Decides when a task should run
    // First-fit over the supplied idle-worker order
    /**
     * Ex:
     * idle_workers = [0,1,2]
     * 0-> insufficient availble resources
     * 1-> sufficient available resources
     * 2-> sufficient available resources
     * Result: worker_1 (stop at FIRST worker that can satisfy the request)
     */
    /**
     * It does NOT:
     *   - Acquire resources
     *   - reserve workers
     *   - mutate ResourceManager
     *   - mutate WorkerPool
     *   - enqueue/dequeue task
     *   - change task lifecycle state
     * ONLY answers: Given these idle workers, which one should I TRY to use
     * 
     * scheduler will perform the actual reservation and resource acquisition
     */
    class PlacementPolicy
    {
    // Intentially no private state as the object represents policy not runtime state
    public:
        [[nodiscard]] std::optional<WorkerID> SelectWorker(
            const ResourceRequest& request, 
            const std::vector<WorkerID>& idle_workers, 
            const ResourceManager& resources) const;
    };

}
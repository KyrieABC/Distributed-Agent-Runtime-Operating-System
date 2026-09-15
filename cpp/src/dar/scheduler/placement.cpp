#include "dar/scheduler/placement.h"

namespace dar
{
    std::optional<WorkerID> PlacementPolicy::SelectWorker(
        const ResourceRequest& request, 
        const std::vector<WorkerID>& idle_workers, 
        const ResourceManager& resources) const
    {
        // Deterministic first-fit placement
        // First appeared available will be used first
        for(const WorkerID& worker_id : idle_workers)
        {
            // Placement asks about AVAILABLE capacity, not Total Capacity
            // CanFitTotal: Could this worker ever run this task 
            // CanFitAvailable: Can this worker run the task right now
            if(resources.CanFitAvailable(worker_id, request))
            {
                // we found the first currrently feasible worker
                // return only identity
                // Scheduler will perform the selection and resource reservation, NOT Here
                return worker_id;
            }
        }
        
        // NO currently available idle worker can satisfy the request
        return std::nullopt;
    }

}
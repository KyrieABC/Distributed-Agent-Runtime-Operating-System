#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "dar/common/status.h"
#include "dar/common/id.h"
#include "dar/runtime/task_manager.h"
#include "dar/worker/worker.h"

/**
 * worker_ids_ -> ordering
 * workers_ -> lookup
 * DONT rely on: std::unordered_map<WorkerID,...>
 */
namespace dar
{
    // WorkerPool
    /**
     * Owns all local worker objects
     * Responsibility:
     *   - which worker(s) exists
     *   - Worker lifecycle
     *   - Worker availability/reservation
     *   - Dispatch to a specific worker
     * Does NOT include: which worker execute this task (PlacementPolicy + Scheduler)
     * Workflow:
     *   Placement Policy (choose WorkerID) -> WorkerPool::TryReserve(id) -> Scheduler acquires resources -> WorkerPool::DispatchReserved(id,work)
     *   (if scheduling fail after reservation) WorkerPool::ReleaseReservation(id) : RESERVED -> IDLE
     */
    class WorkerPool
    {
    public:

        using IdleCallback = std::function<void(WorkerID)>;     

        // Construct exactly worker_count local workers
        // Worker receive stable randomly-generated WorkerIDs
        // Construction does NOT start their threads
        WorkerPool(std::size_t worker_count, TaskManager& task_manager);

        ~WorkerPool();

        WorkerPool(const WorkerPool&) = delete;
        WorkerPool& operator=(const WorkerPool&) = delete;

        // Start every worker thread -> Idempotent at pool level
        Status Start();

        // Drain/shutdown every worker and join its thread -> Idempotent at pool level
        Status Shutdown();

        // Return WorkerIDs in deterministic creation order
        // Do not derive this ordering from unordered_map iteration
        [[nodiscard]] std::vector<WorkerID> WorkerIDs() const;

        // Return worker that are currently IDLE
        // Order matches WorkerIDs()
        [[nodiscard]] std::vector<WorkerID> IdleWorkers() const;

        // Attempt to reserve this exact worker
        // WorkerPool does NOT search for another worker if this fails
        // IDLE -> Reserve
        Status TryReserve(WorkerID worker_id);

        // Undo a reservation: Reserved -> IDLE
        // Used for scheduler roolback before Dispatch()
        Status ReleaseReservation(WorkerID worker_id);

        // Dispatch one WorkIteam to an already-reserved worker
        // Reserved -> Busy
        // The worker thread executes the handler asynchronously
        Status DispatchReserved(WorkerID worker_id, WorkItem work);
    
        void SetIdleCallback(IdleCallback callback);
    private:
        // Called by worker after: BUSY -> IDLE
        // Later the callback can wake scheduler immmediately
        void OnWorkerIdle(WorkerID worker_id);

        // Find worker by ID
        // Caller must hold mu_
        Worker* FindWorkerLocked(WorkerID worker_id);

        // const version of FindWorkerLocked() (caller must hold mu_)
        const Worker* FindWorkerLocked(WorkerID worker_id) const;

        TaskManager& task_manager_;

        mutable std::mutex mu_;

        bool started_{false};

        // keep determinisitc creation order separately
        std::vector<WorkerID> worker_ids_;

        // Worker objects are heap-owned so their addresses remain stable
        // StrongIDHash is required because WorkerID is a StrongID
        std::unordered_map<WorkerID,std::unique_ptr<Worker>,StrongIDHash<WorkerID>> workers_;
    
        IdleCallback idle_callback_;
    };
}
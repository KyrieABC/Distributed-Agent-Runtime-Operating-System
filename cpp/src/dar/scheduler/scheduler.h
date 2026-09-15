#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "dar/common/status.h"
#include "dar/common/id.h"
#include "dar/core/resource.h"
#include "dar/runtime/task_manager.h"
#include "dar/scheduler/placement.h"
#include "dar/scheduler/resource_manager.h"
#include "dar/scheduler/task_queue.h"
#include "dar/worker/worker_pool.h"

namespace dar
{
    // Scheduler
    /**
     * AN orchestration
     * Combines: TaskQueue + TaskManager + PlacementPolicy + ResourceManager + WorkerPool
     * Does NOT replace any of them
     * 
     * Responsibilities:
     *   - maintain strict FIFO scheduling
     *   - examine ONLY the queue head
     *   - find currently idle workers
     *   - ask PlacementPolicy for first-fit placement
     *   - reserve the selected worker
     *   - acquire its logical resources
     *   - remove the task from the queue
     *   - create an ExecutionID
     *   - transition Queued -> Scheduled 
     *   - construct WorkItem
     *   - dispatch it
     *   - release resources after worker completion
     * 
     * Scheduler does NOT:
     *   execute TaskHandler / construct RuntimeContext / mutate ExecutionState directly / implement resource arithmetic / implement placement policy / reorder the FIFO queue 
     */
    class Scheduler
    {
    public:
        Scheduler(TaskQueue& task_queue, 
            TaskManager& task_manager,
            PlacementPolicy& placement,
            ResourceManager& resource_manager,
            WorkerPool& worker_pool
        );

        ~Scheduler();

        Scheduler(const Scheduler&) = delete;
        Scheduler operator=(const Scheduler&) = delete;

        // Start the scheduler orchestration thread
        // WorkerPool should already have been started by Runtime
        Status Start();

        // Stop scheduliing new work and join the scheduler thread
        // WorkerPool::Shutdown() is responsible for draining work that has already been dispatched
        Status Shutdown();

        // Add an already-registered task to scheduling
        /**
         * Expected lifecycle before call:
         *   Pending
         * Success: Pending -> Queued (TaskID appended to TaskQueue)
         */
        Status Enqueue(TaskID task_id);

        // Cancel work through the scheduler
        /**
         * Queued:
         *   remove it from TaskQueue then TaskManager performs Queued -> Cancelled
         * if RUNNING: TaskManager only sets the cooperative cancellation token
         */
        Status Cancel(TaskID task_id);

        // Wait until every accepted task has left the queue
        // and every dispatched execution has completed
        // Runtime::Shutdown() uses this BEFORE Scheduler::Shutdown()
        Status Drain();

        //number of tasks still waiting in the FIFO queue
        [[nodiscard]] std::size_t Queue_size() const
        {
            return task_queue_.Size();
        }
    private:
        // Main scheduler thread
        void Run();

        // Attempt to schedule exactly the current FIFO head
        /**
         * Returns OK both when:
         *   - one task was successfully dispatched
         *   - the FIFO head cannot run right now
         * The caller determines whether immediate scheduling pass should occur
         */
        Status TryScheduleOne(bool* dispatched);

        // Called through WorkerPool after a worker completes its WorkItem and returns BUSY -> IDLE
        void OnWorkerIdle(WorkerID worker_id);

        struct RunningReservation
        {
            TaskID task_id;
            ResourceRequest resources;
        };

        TaskQueue& task_queue_;
        TaskManager& task_manager_;
        PlacementPolicy& placement_;
        ResourceManager& resource_manager_;
        WorkerPool& worker_pool_;

        mutable std::mutex mu_;
        std::condition_variable cv_;

        bool started_{false};
        bool stopping_{false};

        // Set whenever some event may make scheduling progress possible:
        /**
         *   Enqueue()
         *   worker becomes IDLE
         *   Cancellation removes FIFO head
         */
        bool wake_requested_{false};

        // Resources acquired by Scheduler but not yet released
        // Since one worker executes at most one WorkItem, WorkerID is enough to identify its current resouce reservation
        std::unordered_map<WorkerID, RunningReservation,StrongIDHash<WorkerID>> running_;

        std::thread threads_;
    };
}
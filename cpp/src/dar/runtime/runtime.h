#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>

#include "dar/common/status.h"
#include "dar/core/resource.h"
#include "dar/core/task_spec.h"
#include "dar/runtime/task_manager.h"
#include "dar/scheduler/placement.h"
#include "dar/scheduler/resource_manager.h"
#include "dar/scheduler/scheduler.h"
#include "dar/scheduler/task_queue.h"
#include "dar/worker/executor.h"
#include "dar/worker/worker_pool.h"

namespace dar
{

    // RuntimeOptions
    // Configuration for one local DAR runtime
    struct RuntimeOptions
    {
        // number of local execution workers
        std::size_t worker_count{1};

        /**
         * Logical resources owned by EACH worker
         * Ex: worker_count=4, resources_per_workers={CPU:2}
         */

         ResourceSet resources_per_worker;
    };

    // Runtime
    /**
     * Public facade(outward appearance) of Phase 1 local DAR kernel
     * 
     * Runtime owns:
     *  -> TaskManager
     *  -> ResourceManager
     *  -> WorkerPool
     *  -> TaskQueue
     *  -> PlacementPolicy
     *  -> Scheduler
     * 
     * Callers do NOT need to coordinate these componenets themselves
     * 
     * Lifecycle:
     *   construct -> Start() -> Submit / Cancel / GetStatus / GetResult -> Shutdown()
     */
    class Runtime
    {
    public:
        explicit Runtime(RuntimeOptions options);

        ~Runtime();

        Runtime(const Runtime&) = delete;
        Runtime& operator=(const Runtime&) = delete;

        /**
         * Initialize the local runtime
         * Ordering:
         *   Register logical resources -> Start worker thread -> start scheduler -> accepting = true
         */
        Status Start();

        /**
         * Gracefully stop the runtime
         * Ordering:
         *   accepting = false -> drain accepted work -> stop scheduler -> stop workers
         */
        Status Shutdown();

        /**
         * Submit one logical task
         * 
         * Admission control checks TOTAL worker capacity,
         * not currently available capacity
         * 
         * Impossible task (>total capacity) -> Reject immediately
         * Feasible but current unavailable(<total,>currentAvailable) -> Accepted but waits in FIFO
         */
        Status Submit(TaskSpec spec, TaskHandler handler);

        // Cancel queued/running work
        Status Cancel(TaskID id);

        // Non-blocking task metadata quer
        Status GetStatus(TaskID id, TaskSnapshot* out) const;

        // Wait for terminal task result
        Status GetResult(
            TaskID id,
            std::string* out,
            std::chrono::milliseconds timeout = std::chrono::milliseconds::max()
        ) const;

        // Number of tasks waiting in Scheduler's FIFO queue
        [[nodiscard]] std::size_t QueueSize() const
        {
            // The Queue_size() function within scheduler
            return scheduler_.Queue_size();
        }
    private:
        RuntimeOptions options_;

        // Protects Runtime lifecycle/admission state
        // This mutex does NOT protect TaskManager, WorkerPool, etc
        // Those components own their own synchronization
        mutable std::mutex lifecycle_mu_;

        bool started_{false};
        bool accepting_{false};

        // Worker resources must only be registered once
        // ResourceManager rejects duplicate registration, so restarting
        // Runtime must not register the same workers again
        bool resources_initialized_{false};

        /**
         * Dependency order matters
         * 
         * C++ constructs members in declaration order
         * WorkerPool requires TaskManager&
         * 
         * Scheduler requires:
         *   TaskQueue
         *   TaskManager
         *   PlacementPolicy
         *   ResourceManager
         *   WorkerPool
         */
        TaskManager task_manager_;
        ResourceManager resource_manager_;
        WorkerPool worker_pool_;
        TaskQueue task_queue_;
        PlacementPolicy placement_;
        Scheduler scheduler_;
    };
}
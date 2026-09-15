#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include "dar/common/status.h"
#include "dar/common/id.h"
#include "dar/core/task_spec.h"
#include "dar/runtime/runtime_context.h"
#include "dar/runtime/task_manager.h"
#include "dar/worker/worker.h"

namespace dar
{
    // WorkerState
    /**
     * Local execution-slot lifecycle:
     *   IDLE - Reserve() -> Reserved -> Busy - Execution finishes -> IDLE
     * Reserve is very important!
     */
    enum class WorkerState
    {
        kIdle = 0,
        kReserved,
        kBusy,
    };

    // WorkItem
    /**
     * Everything a worker needs to execute ONE already-scheduled attempt
     * 
     * Worker does NOT query TaskManager,
     * Scheduler/WorkerPool prepares the WorkItem after: Queued -> Scheduled
     *   then hands it to the reserved worker
     */
    struct WorkItem
    {
        TaskSpec spec;
        ExecutionID execution_id;
        TaskHandler handler;

        RuntimeContext::CancellationToken cancel_token;
    };

    // Worker
    /**
     * Worker owns ONE execution thread
     * 
     * Responsibilites:
     *   - maintain IDLE / RESERVED / BUSY state
     *   - accept one WorkItem
     *   - execute one handler at a time
     *   - construct RuntimeContext
     *   - tell TaskManager when execution start/completes
     *   - notify the owner when it becomes idle again
     * 
     * Worker Does NOT : select tasks/chooce placement/acquire ResourceManager capacity/Enqueue tasks/decide lifecycle legality
     */
    class Worker
    {
    public:
        // called after execution finishes and worker has returned to IDLE
        using IdleCallback = std::function<void(WorkerID)>;

        Worker(WorkerID id, TaskManager& task_manager, IdleCallback idle_callback={});

        ~Worker();

        // = delete to a constructor explicitly disables it
        Worker(const Worker&) = delete;

        // Worker contains std::mutex, std::condition_variable, std::thread
        /**
         * Those synchronization object should not be treated as copy-assignable worker state
         *  All not copyable.
         */
        Worker& operator=(const Worker& ) = delete;

        // Start the worker thread
        Status Start();

        // Stop the worker thread and join it
        Status Shutdown();

        [[nodiscard]] WorkerID id() const noexcept
        {
            return id_;
        }
        [[nodiscard]] WorkerState state() const;
        [[nodiscard]] bool IsIdle() const;

        /**
         * Atomically claim this worker
         * Success: IDLE -> Reserved
         * Failure: Reserved -> unchanged   BUSY -> unchanged
         */
        Status Reserve();

        /**
         * Given an already Reserved worker exactly one WorkItem
         * 
         * Success: Reserved -> Busy
         * and wake the worker thread
         * 
         * Handler does NOT execute on the caller's thread
         */
        Status Dispatch(WorkItem work);

        /**
         * Undo a reservation that has not yet been dispatched
         * Reserved -> IDLE
         * 
         * Needed when scheduler reserves this worker but a later scheduling step fails before Dispatch()
         */
        Status ReleaseReservation();

        void Run();
    private:
        WorkerID id_;

        // TaskManager remains authoritative for task lifecycle
        TaskManager& task_manager_;

        IdleCallback idle_callback_;
        Executor executor_;

        mutable std::mutex mu_;
        std::condition_variable cv_;

        WorkerState state_{WorkerState::kIdle};

        // One-worker mailbox 
        // 1 worker is not a queue (TaskQueue owns waiting tasks)
        // 1 worker owns at most 1 dispatched executions
        std::optional<WorkItem> mailbox_;
        bool started_{false};
        bool stopping_{false};

        std::thread thread_;
    };
}
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "dar/common/status.h"
#include "dar/common/id.h"
#include "dar/core/lifecycle.h"
#include "dar/core/task_spec.h"
#include "dar/worker/executor.h"

namespace dar
{
    // TaskSnapShot
    /**
     * Read-only metadata returned to callers
     * 
     * Do NOT expose internal Record(executable code, synchronization primitives)
     * 
     * SnapShot is just an observation of task state
     */
    struct TaskSnapshot
    {
        TaskID id;
        ExecutionState state{ExecutionState::kPending};
        std::optional<WorkerID> worker;

        std::uint32_t attempt{0};
        ExecutionID execution_id;
    };

    // TaskManager
    /**
     * Authoritative owner of logical task state
     * 
     * Lifecycle:
     * Pending -> Queued -> Scheduled-> Running -> Succeeded/Failed/Cancelled
     * 
     * No other runtime component should arbitrarily modify ExecutionState
     * 
     * Scheduler/Worker instead request transitions through:
     *   MarkQueued(), MarkScheduled(), MarkRunning(), Complete(), Cancel()
     */
    class TaskManager
    {
    public:
        TaskManager() = default;
        TaskManager(const TaskManager&) = delete;
        TaskManager& operator=(const TaskManager&) = delete;

        /**
         * Register a new logical task
         * 
         * Initial: state(kPending), attempt(0)
         */
        Status Register(TaskSpec, TaskHandler handler);

        // Pending -> Queued
        Status MarkQueued(TaskID id);

        /**
         * Queued -> scheduled
         * 
         * Where a concrete execution attempt becomes associated with a worker
         */
        Status MarkScheduled(TaskID id, WorkerID worker_id, ExecutionID execution_id);

        // Scheduled -> Running
        Status MarkRunning(TaskID);

        /**
         * Complete a RUNNING execution
         * 
         * outcome.status determines the terminal state
         * 
         * OK -> Succeeded 
         * Cancelled -> cancelled
         * Otherwise -> Failed
         */
        Status Complete(TaskID id, ExecutionOutcome outcome);

        // Request Cancellation
        /**
         * Queued -> Cancelled immediately
         * Running -> Cancellation token becomes true, but state remains RUNNING
         * 
         * RUnning handler must observe RuntimeContext and return
         */
        Status Cancel(TaskID id);

        // Read task metadata without waiting
        Status GetSnapshot(TaskID id, TaskSnapshot* out) const;

        // Wait until the task enters a terminal state
        Status GetResult(TaskID id, std::string* out, std::chrono::milliseconds timeout = std::chrono::milliseconds::max()) const;

        // Execution data access
        /**
         * Scheduler/Worker  need the task description, handler and cancellation token to construct a WorkItem/RuntimeContext
         */
        Status GetSpec(TaskID id, TaskSpec* out) const;

        Status GetHandler(TaskID id, TaskHandler* out) const;

        Status GetCancellationToken(TaskID id,std::shared_ptr<std::atomic_bool>* out) const;
    private:
        struct Record{
            TaskSpec spec;
            TaskHandler handler;

            ExecutionState state{ExecutionState::kPending};

            std::optional<WorkerID> worker;

            std::uint32_t attempt{0};
            ExecutionID execution_id;

            // Shared with RuntimeContext
            std::shared_ptr<std::atomic_bool> cancel_token{std::make_shared<std::atomic_bool>(false)};

            // meaningful once state is terminal
            Status terminal_status;
            std::string result;

            // GetResult() waits on this
            mutable std::condition_variable cv;
        };

        // Centralized lifecycle validation
        // Caller must already hold mu_
        Status TransitionLocked(Record& record, ExecutionState next_state);

        // Caller MUST already hold mu_
        static bool IsTerminal(ExecutionState state) noexcept;

        mutable std::mutex mu_;

        // Record contains a condition_variable, so store records indirectly
        std::unordered_map<TaskID, std::unique_ptr<Record>, StrongIDHash<TaskID>> records_;
    };
}
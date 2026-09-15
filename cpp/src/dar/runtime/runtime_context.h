#pragma once

// Provides types and operations for safe, lock-free concurrent data access across threads.
#include <atomic>
// Provide core utilities for modern, safe, efficient resource management (foundation of C++ RAII)
#include <memory>

#include "dar/core/lifecycle.h"
#include "dar/core/task_spec.h"

namespace dar
{

    // RuntimeContext is the execution-level view presented to a TaskHandler
    /**
     * Ownership Rule:
     * TaskManager/Runtime   -> Owns task lifecycle and cancellation decisions
     * Worker/Executor      -> Owns execution mechanics
     * RuntimeContext        -> Exposes execution identity + cancellation state
     * TaskHandler           -> Observes RuntimeContext
     */
    /**
     * RuntimeContext does NOT expose: Cancel(), MarkSucceeded(), MarkFailed(), MarkCancelled()
     * -> Belong to Runtime's control plane, not user task code
     */
    // RuntimeContext does not produce its own cancellation token
    // -> The cancellation originate from authoritative task record
    class RuntimeContext
    {
    public:
        /**
         * Cancellation state is shared between:
         *   TaskManager: store(true) when calcenllation is requested
         *   RuntimeContext: Load() from the worker thread
         */
        // shared_ptr gives the cancellation state an independent lifetime:
        // -> The state remains alive far as long as an execution still observes it

        using CancellationToken = std::shared_ptr<std::atomic_bool>;

        /**
         * 1 RuntimeContext represents 1 concrete execution attempt
         * task_id: Stable identity of the logical task
         * exeecution_id: Identity of this particular execution attempt
         * worker_id: Worker currently executing the attempt
         * cancellation_token: Shared state owned jointly by task record / execution path
         */

        RuntimeContext(TaskID task_id, ExecutionID execution_id, WorkerID worker_id, CancellationToken cancellation_token);

        // TaskID stays stable across future retries
        /**
         *Ex:TaskID : T42
         *     Attempt 1 -> ExecutionID : E10
         *     Attempt 2 -> ExecutionID : E11
         *   Both belongs to T42
         */

         const TaskID& task_id() const noexcept
         {
            return task_id_;
         }

         // Identify the Current execution attempt, not the logical task
         const ExecutionID execution_id() const noexcept 
         {
            return execution_id_;
         }

        // Idnetifies the local worker executing this attempt

        const WorkerID& worker_id() const noexcept
        {
            return worker_id_;
        }

        /**
         * Co-operative cancellation query
         * 
         * This function does not cancel anything itself
         * Answers: has the runtime requested that this execution stop
         * 
         * Long-running handlers are expected to call this at sensible interruption points and return Status::Cancelled() when they safely stop
         */
        bool IsCancellationRequested() const noexcept;

    private:
        // Execution Identity is immutable from thee TaskHandler's perspective
        // There are intentionally no setters
        TaskID task_id_;
        ExecutionID execution_id_;
        WorkerID worker_id_;

        // Shared rather than embedded because cancellation is requested by another component/thread
        // -> By TaskManager via Runtime::Cancel
        CancellationToken cancellation_token_;
    };
}
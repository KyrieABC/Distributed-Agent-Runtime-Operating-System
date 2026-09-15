#pragma once

// collection of class templates and utilities designed for functoinal programming, function object creation, behavior wrapping
#include <functional>
#include <string>

#include "dar/common/status.h"
#include "dar/core/task_spec.h"
#include "dar/runtime/runtime_context.h"

namespace dar
{
    // Execution Outcome
    /**
     * Represent what happened during ONE concrete execution attempt
     * 
     * Executor does NOT turn this into: K(state)
     * -> Lifecycle decisoin belongs to worker + task manager
     * 
     * Executor only reports: "Status returned by execution", "result to payload"
     */
    struct ExecutionOutcome
    {
        Status status;
        std::string result;
    };
    
    // Task Handler
    /**
     * Executable code attached to a TaskSpec
     * 
     * Important separation:
     *   TaskSpec : Description of WHAT should execute
     *   TaskHandler : Actual local c++ code that performs the work
     * 
     * This keeps TaskSpec as data rather than putting std::function inside the task's logical/distributed representation
     */
    using TaskHandler = std::function<Status(const TaskSpec&, RuntimeContext&, std::string*)>;

    // Executor
    /**
     * Executor defines the smallest execution boundary in Phase 1:
     *     1. TaskSpec 
     *    2. RuntimeContext
     *    3. TaskHandler
     *    4. Executor
     *    5. ExecutionOutcome
     * 
     * Executor deliberately does not know: TaskManager, WorkerPool, Scheduler, TaskQueue, ResourceManager, placement, retries, task lifecycle transition
     * 
     * Executor responsible for:
     *   - Invoke handler
     *   - Collect its result
     *   - Preserve the returned Status
     *   - Contain unexpected exceptions 
     */
    class Executor
    {
    public: 
        [[nodiscard]] ExecutionOutcome Execute(const TaskSpec& spec, RuntimeContext& context, const TaskHandler& handler) const noexcept;
    };
}
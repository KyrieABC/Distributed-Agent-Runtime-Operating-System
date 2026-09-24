#include "dar/worker/executor.h"

#include <exception>
#include <string>
#include <utility>

/**
 * Why Executor doesn't convert Status -> ExecutioState
 * 
 * Executor only depends on: TaskManager, ExecutionState, task lifecycle
 * Later:
 * ExecutionOutcome -OK-> TaskManager::Complete(...) -> Succeeded
 * ExecutionOutcome -Cancelled-> TaskManager::Complete(...) -> Cancelled
 * ExecutionOutcome -Error-> TaskManager::Complete(...) -> Failed
 */
namespace dar
{
    ExecutionOutcome Executor::Execute(const TaskSpec& spec, RuntimeContext& context, const TaskHandler& handler) const noexcept
    {
        // Guard against empty Task handler
        /**
         * TaskHandler is a std::function
         * -> A default-constructed std::function may contain no callable target
         * Calling such function would throw std::bad_function_call
         * 
         * Treating this explicitly as an internal execution failure gives us a clearer runtime error than relying on std::function exception
         */
        if(!handler)
        {
            // Initiate a temporary result struct containing two fields: status and result
            // Value within {} directly assigned to two member variables
            return ExecutionOutcome{
                Status::Internal("executor received an empty task handler"),""
            };
        }
        try 
        {
                // Phase 1 result storage
                /**
                 * For now a result is simply an opaque std::string
                 * 
                 * Later: std::string -> ObjectRef / ValueRef
                 * 
                 */
                std::string result;

                // Execute exactly one TaskHandler
                /**
                 * Executor does not: context.IsCancellationRequested() automaticaly
                 * Concellation is coooperative
                 * 
                 * TashHandler decides WHERE it is safe to stop
                 * if(context.IsCancellationRequested())
                 * {
                 *    return Status::Cancelled("...");
                 * }
                 * 
                 * Executor simply preserves whatever Status the handler returns
                 */
                Status status = handler(spec, context, &result);

                //Preserve the handler's outcome
                /**
                 * Ex:
                 * Status::OK() -> ExecutionOutcome(OK, result) 
                 * Status::Cancelled(...) -> ExecutionOutcome(cancelled,result)
                 * Status::Internal(...) -> ExecutionOutcome(Internal, result)
                 * 
                 * Executor DOES NOT ranslate these into ExecuteState values
                 */
                return ExecutionOutcome{
                    std::move(status),
                    std::move(result)};
            }
            catch(const std::exception& ex)
            {
                // Convert Ordinary C++ exceptions into the runtime's Status model
                /**
                 * Execute is noexcept
                 * -> exception must NOT escape this function, otherwise C++ invoke std::terminal()
                 * 
                 * A TaskHandler is executable/user-provided code
                 * -> so this boundary (catch) protects the Worker thread from an accidental: std::runtime_error(...)
                 * Instead:
                 * exception -> Executor -> Internal Status -> Worker/TaskManager later decide Failed
                 */
                return ExecutionOutcome{
                    Status::Internal(std::string("task handler throw exception: ") + ex.what()),
                    ""
                };
            }
            catch(...)
            {
                // Catch-all
                return ExecutionOutcome{
                    Status::Internal("task handler threw unknow exception"),
                    ""
                };
            }
    }
}
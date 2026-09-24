#include "dar/runtime/runtime_context.h"

// single, powerful preprocessor macro: assert()
#include <cassert>
#include <utility>

namespace dar
{
    RuntimeContext::RuntimeContext(TaskID task_id, ExecutionID execution_id, WorkerID worker_id, CancellationToken cancellation_token)
    : task_id_(std::move(task_id)),execution_id_(std::move(execution_id)), worker_id_(std::move(worker_id)), cancellation_token_(std::move(cancellation_token))
    {
        /**
         * RuntimeContext is an internal runtime object
         * 
         * Every scheduled execution is expected to have one
         * cancellation state created by TaskManager before the worker begins execution
         * 
         * A null token therefore indicates an internal wiring/invariant bug, not a normmal user-facing condition
         */

        // assert(): see if the first impression evaluate to true and if not, the second argument is custom error message
        // using CancellationToken = std::shared_ptr<std::atomic_bool>; (CancellationToken cancellation_token)
        assert(cancellation_token_ != nullptr && "RuntimeContext requires a cancellation token");
    }

    bool RuntimeContext::IsCancellationRequested() const noexcept
    {
        /**
         * Keep the null check even though the constructor uses assert()
         * -> assert() may be compiled out in release builds
         * --> This defensive branch prevents an accidental null token from becoming a null-pointer reference
         */

        // Under the intended runtime invariants this branch is never taken
        if(!cancellation_token_)
        {
            return false;
        }

        /**
         * TaskManager::Cancel() publishes:
         *   cancel_token->store(true, std::memory_order_release)
         * 
         * Worker observes it using an acquire load
         * 
         * That gives us an explicit synchronization pair:
         *   control thread                 worker thread
         *   Store(true,release)------------> (Load acquire)
         */

        // Use acquire here gives the cancellation signal a clear synchronization contract if calcellation-related state is later published before the release store
        return cancellation_token_->load(std::memory_order_acquire);
    }
}
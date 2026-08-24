#pragma once

#include <string_view>
#include "dar/common/status.h"

namespace dar
{
    /**
     * Execution Lifecycle for a single execution
     * 
     * State changes must go through Lifecycle::TransitionTo() 
     */
    enum class ExecutionState
    {
        kPending = 0,
        kQueued,
        kScheduled,
        kRunning,
        kSSucceeded,
        kFailed,
        kCancelled,
    };

    // Return a stable human-readable name for execution state
    [[nodiscard]] std::string_view ExecutionStateName(ExecutionState state) noexcept;
    // Return true if no further execution-state transition are allowed
    [[nodiscard]] bool IsTerminal(ExecutionState state) noexcept;
    /**
     * Valid normal transition:
     * Pending -> Queued
     * Queued -> Scheduled
     * Scheduled -> Running
     * Running -> Succeeded 
     * Running -> failed
     * 
     * Cancellation is also allowed from any non-terminal state
     * Pending -> Cancelled
     * Queued -> Cancelled
     * Scheduled -> Cancelled
     * Running -> Cancelled
     */
    [[nodiscard]] bool CanTransition(ExecutionState from, ExecutionState to) noexcept;

    class Lifecycle final
    {
    public:
        // New execution begins in the pending state
        Lifecycle() = default;

        // Useful when reconstructing persisted state or in test
        // Could be later make resotration a factory such as Lifecycle::Restore(...), leave normal construction always kPending
        explicit Lifecycle(ExecutionState initial): state_(initial) {}
        [[nodiscard]] ExecutionState state() const noexcept
        {
            return state_;
        }
        [[nodiscard]] bool terminal() const noexcept
        {
            return IsTerminal(state_);
        }

        /**
         * Perform a validated state transition
         * 
         */
        Status TransitionTo(ExecutionState next);
    private:
        // Because state_ is private, external code must call TransitionTO(state)
        ExecutionState state_{ExecutionState::kPending};
    };
}
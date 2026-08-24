#include "dar/core/lifecycle.h"

#include <string>

namespace dar
{
    std::string_view ExecutionStateName(ExecutionState state) noexcept
    {
        switch(state)
        {
            case ExecutionState::kPending:
                return "Pending";
            case ExecutionState::kQueued:
                return "Queued";
            case ExecutionState::kScheduled:
                return "Scheduled";
            case ExecutionState::kRunning:
                return "Running";
            case ExecutionState::kSSucceeded:
                return "Succeeded";
            case ExecutionState::kFailed:
                return "Failed";
            case ExecutionState::kCancelled:
                return "Cancelled";
        }
        return "Unknown";
    }

    bool IsTerminal(ExecutionState state) noexcept
    {
        switch(state)
        {
            case ExecutionState::kSSucceeded:
            case ExecutionState::kFailed:
            case ExecutionState::kCancelled:
                return true;
            case ExecutionState::kPending:
            case ExecutionState::kQueued:
            case ExecutionState::kScheduled:
            case ExecutionState::kRunning:
                return false;
        }
        // None of the state? How?
        return false;
    }

    bool CanTransition(ExecutionState from, ExecutionState to) noexcept
    {
        // Cancellation is valid from any non-terminal state
        if(!IsTerminal(from)&&to==ExecutionState::kCancelled)
        {
            return true;
        }

        switch(from)
        {
            case ExecutionState::kPending:
                return to == ExecutionState::kQueued;
            case ExecutionState::kQueued:
                return to == ExecutionState::kScheduled;
            case ExecutionState::kScheduled:
                return to == ExecutionState::kRunning;
            case ExecutionState::kRunning:
                return to == ExecutionState::kSSucceeded || to == ExecutionState::kFailed;
            case ExecutionState::kSSucceeded:
            case ExecutionState::kFailed:
            case ExecutionState::kCancelled:
                return false;
        }
        return false;
    }

    Status Lifecycle::TransitionTo(ExecutionState next)
    {
        if(!CanTransition(state_,next))
        {
            std::string message = "Invalid execution state transition: ";
            message += ExecutionStateName(state_);
            message += " -> ";
            message += ExecutionStateName(next);
            return Status::FailedPrecondition(std::move(message));
        }

        // If can transition
        state_ = next;
        return Status::OK();
    }
}
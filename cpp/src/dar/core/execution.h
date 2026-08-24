#pragma once

#include <optional>

#include "dar/common/id.h"
#include "dar/common/status.h"
#include "dar/common/types.h"
#include "dar/core/lifecycle.h"
#include "dar/core/resource.h"
#include "dar/common/time.h"

namespace dar
{
    /**
     * 1 concrete attempt to execute a logical task
     * 
     * TaskID identifies the logical work
     * ExecutionID identifies this specific attempt
     * 
     * Retrying a task therefore preserves TaskID while create a new ExecutionID with an incremented AttemptNumber
     */

    class Execution final
    {
    public:
        Execution(ExecutionID id, 
            TenantID tenant_id, 
            TaskID task_id, 
            AgentID agent_id, 
            AttemptNumber attempt, 
            ResourceRequest resources, 
            WallTime created_at);

        // Validates the structural invariants of this execution
        [[nodiscard]] Status Validate() const;

        // Performs a validated lifecycle transition and records timestamps associated with execution progress
        // State validation occurs before timestamps are modified
        Status TransitionTo(ExecutionState next, WallTime at);

        [[nodiscard]] const ExecutionID& id() const noexcept
        {
            return id_;
        }
        
        [[nodiscard]] const TenantID& tenant_id() const noexcept
        {
            return tenant_id_;
        }

        [[nodiscard]] const AgentID& agent_id() const noexcept
        {
            return agent_id_;
        }

        [[nodiscard]] AttemptNumber attempt() const noexcept
        {
            return attempt_;
        }

        [[nodiscard]] const ResourceRequest& resourecs() const noexcept
        {
            return resources_;
        }

        [[nodiscard]] const TaskID& task_id() const noexcept
        {
            return task_id_;
        }

        [[nodiscard]] ExecutionState state() const noexcept
        {
            return lifecycle_.state();
        }

        [[nodiscard]] WallTime created_at() const noexcept
        {
            return created_at_;
        }

        [[nodiscard]] const std::optional<WallTime>& started_at() const noexcept
        {
            return started_at_;
        }

        [[nodiscard]] const std::optional<WallTime>& finished_at() const noexcept
        {
            return finished_at_;
        }

        [[nodiscard]] bool terminal() const noexcept
        {
            return lifecycle_.terminal();
        }

    private:
        ExecutionID id_;
        TenantID tenant_id_;
        TaskID task_id_;
        AgentID agent_id_;

        AttemptNumber attempt_{1};
        ResourceRequest resources_;
        Lifecycle lifecycle_;

        WallTime created_at_;
        std::optional<WallTime> started_at_;
        std::optional<WallTime> finished_at_;
    };
}
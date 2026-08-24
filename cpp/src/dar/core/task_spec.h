// Answer: How/Where is this particular execution happening
// TasID remain throughout retries (separated)
// WorkerID, NodeID, are attempt-specific state

#pragma once

#include <string>
#include <vector>

#include "dar/common/id.h"
#include "dar/common/status.h"
#include "dar/core/resource.h"
#include "dar/core/object_ref.h"

namespace dar
{
    struct TaskSpec final
    {
        // Tenant that owns this logical task
        TenantID tenant_id;

        // Stable logical identity of this task
        TaskID id;

        // Logical agent responsible for this task
        AgentID agent_id;

        std::string name;

        // resource required to execute this task
        ResourceRequest resources;

        /**
         * Logical input object references
         * Object Location, transport, and resolution are deliberately outside
         * TaskSpec at this phase
         */
        std::vector<ObjectRef> inputs;
        [[nodiscard]] Status Validate() const;

    };
}
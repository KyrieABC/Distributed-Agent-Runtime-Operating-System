#pragma once

// Type-safe, high-precision framework for handling dates and times
#include <chrono>
#include <cstdint>
#include <string>

#include "dar/common/id.h"
#include "dar/common/status.h"
#include "dar/core/resource.h"

namespace dar
{
    struct RetryPolicy final
    {
        // Total number of execution attempt, including the initial attempt
        // Total attempt, not number of retries (1 attempt and done here)
        std::uint32_t max_attempt{1};

        // Delay before the first try
        std::chrono::milliseconds initial_backoff{100};

        // Upper bound on retry delay
        std::chrono::milliseconds max_backoff{10000};

        // Exponential backoff multiplier
        double multiplier{2.0};

        [[nodiscard]] Status Validate() const;
    };

    struct AgentSpec final
    {
        // Tenant that owns this logical agent
        TenantID tenant_id;

        // Stale logical identity of the agent
        AgentID id;

        std::string name;

        // Default resource requirement for executions of this agent
        ResourceRequest resources;

        RetryPolicy retry_policy;

        [[nodiscard]] Status Validate() const;
    };
}
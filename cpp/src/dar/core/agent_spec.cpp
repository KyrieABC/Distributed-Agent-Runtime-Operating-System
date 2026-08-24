#include "dar/core/agent_spec.h"

#include <cmath>

#include "dar/common/error.h"

namespace dar
{
    Status RetryPolicy::Validate() const
    {
        if(max_attempt == 0)
        {
            return Status::InvalidArgument("Retry policy max_attempts must be at least 1");
        }

        if(initial_backoff.count()<0)
        {
            return Status::InvalidArgument("retry policy initial backoff cannot be negative");
        }

        if(max_backoff.count()<0)
        {
            return Status::InvalidArgument("retry policy max_backoff cannot be negative");
        }

        if(max_backoff<initial_backoff)
        {
            return Status::InvalidArgument("retry policy max_backoff cannot be less than initial_backoff");
        }

        if(!std::isfinite(multiplier))
        {
            return Status::InvalidArgument("retry policy multiplier must be finite");
        }

        if(multiplier < 1.0)
        {
            return Status::InvalidArgument("retry policy multiplier must be at least 1.0");
        }

        return Status::OK();
    }

    Status AgentSpec::Validate() const
    {
        if(tenant_id.IsNil())
        {
            return Status::InvalidArgument("agent tenant_id cannot be nil");
        }

        if(id.IsNil())
        {
            return Status::InvalidArgument("agent id cannot be nil");
        }

        if(name.empty())
        {
            return Status::InvalidArgument("agent name cannot be empty");
        }

        DAR_RETURN_IF_ERROR(retry_policy.Validate());

        return Status::OK();
    }
}
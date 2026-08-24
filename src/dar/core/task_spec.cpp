#include "dar/core/task_spec.h"

#include "dar/common/error.h"

namespace dar
{
    Status TaskSpec::Validate() const
    {
        if(tenant_id.IsNil())
        {
            return Status::InvalidArgument("task tenant_id cannot be nil");
        }

        if(id.IsNil())
        {
            return Status::InvalidArgument("task id cannot be nil");
        }

        if(agent_id.IsNil())
        {
            return Status::InvalidArgument("task agent_id cannot be nil");
        }

        if(name.empty())
        {
            return Status::InvalidArgument("task name cannot be empty");
        }

        for(const auto& input : inputs)
        {
            DAR_RETURN_IF_ERROR(input.Validate());
        }

        return Status::OK();
    }
}
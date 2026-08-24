#include "dar/core/execution.h"

#include <utility>

namespace dar
{
    Execution::Execution(
        ExecutionID id,
        TenantID tenant_id, 
        TaskID task_id,
        AgentID agent_id,
        AttemptNumber attempt,
        ResourceRequest resources,
        WallTime created_at):id_(id), tenant_id_(tenant_id), task_id_(task_id), agent_id_(agent_id), attempt_(attempt),resources_(resources), lifecycle_(), created_at_(created_at){}
    
    Status Execution::Validate() const
    {
        if(id_.IsNil())
        {
            return Status::InvalidArgument("Execution ID cannot be nil");
        }
        // ? comparison
        if(attempt_==0)
        {
            return Status::InvalidArgument("attempt number cannot be less or equal to 0");
        }
        return Status::OK();
    }

    Status Execution::TransitionTo(ExecutionState next, WallTime at)
    {
        // No execution event may occur before it was created
        if(at<created_at_)
        {
            return Status::InvalidArgument("Transition time procedes execution creation time");
        }

        if(started_at_.has_value() && at < *started_at_)
        {
            return Status::InvalidArgument("Transition time precedes execution start time");
        }
        // validate and perform the lifecycle transition before changing any timestamp metadata
        const Status status = lifecycle_.TransitionTo(next);

        if(!status.ok())
        {
            return status;
        }

        // Record the first moment this execution actaully begins running
        // has_value() check protects started_at_ from being overwritten if the lifecycle becomes more complex in future
        if(next == ExecutionState::kRunning && !started_at_.has_value())
        {
            started_at_ = at;
        }

        // Any terminal state marks the end of this execution attempt
        if(IsTerminal(next))
        {
            finished_at_ = at;
        }

        return Status::OK();
    }

}
#include "dar/control/control_plane.h"

#include <chrono>

#include "dar/serialization/serialization.h"

#include "task.pb.h"

namespace dar
{

namespace
{

constexpr auto kLaunchRpcTimeout =
    std::chrono::seconds(2);

/**
For Phase 3 Stage 5C: 
First ReportTaskResult(T1, E1)
        │
        ├── record is RUNNING
        ├── commit terminal result
        └── Release(Node B, CPU=1)
                    ↓
             schedulable = 1


Duplicate ReportTaskResult(T1, E1)
        │
        ├── record already terminal
        ├── same E1
        ├── same terminal outcome
        └── return OK
                    X
               NO Release()


schedulable remains 1
 */
bool IsTerminal(DistributedTaskState state)
{
    return state == DistributedTaskState::KSucceeded || state == DistributedTaskState::KFailed || state == DistributedTaskState::KCanceled;
}

bool SameStatus(
    const Status& lhs, const Status& rhs
)
{
    return lhs.code() == rhs.code() && lhs.message() == rhs.message();
}
} // namespace


ControlPlane::ControlPlane(
    NodeRegistry& registry,
    NodeManager& node_manager)
    : registry_(registry),
      node_manager_(node_manager)
{
}

/**
 * Before (as of Phase 3 stage 4):
 * reserve -> dispatch -> return IDs 
 * 
 * After (Phase 3 stage 5):
 * reserve -> record E1 -> dispatch E1 -> worker executes
 */

Status ControlPlane::SubmitTask(
    const SubmitTaskSpec& submission,
    SubmitTaskResult* out)
{
    if(out == nullptr)
    {
        return Status::InvalidArgument(
            "submit result must not be null");
    }

    if(submission.tenant_id.IsNil())
    {
        return Status::InvalidArgument(
            "tenant id cannot be nil");
    }

    if(submission.agent_id.IsNil())
    {
        return Status::InvalidArgument(
            "agent id cannot be nil");
    }

    if(submission.entrypoint.empty())
    {
        return Status::InvalidArgument(
            "entrypoint cannot be empty");
    }

    // --------------------------------------------------------
    // 1. Create distributed task identity
    // --------------------------------------------------------

    const TaskID task_id =
        TaskID::Random();

    const ExecutionID execution_id =
        ExecutionID::Random();

    // --------------------------------------------------------
    // 2. CLUSTER SCHEDULER
    //
    // ONLY decides:
    //
    //      Which NodeID?
    // --------------------------------------------------------

    NodeRecord selected_node;

    // selected_node stores the first node found that fits the resources requirement
    Status status =
        registry_.ReserveFirstFit(
            submission.resources,
            &selected_node);

    if(!status.ok())
    {
        return status;
    }

    // From this point forward, the cluster reservation exists.
    //
    // If dispatch fails, roll it back.

    // --------------------------------------------------------
    // 3. Build distributed TaskDescriptor
    // --------------------------------------------------------

    proto::v1::TaskDescriptor task;

    serialization::ToProto(
        task_id,
        task.mutable_task_id());

    serialization::ToProto(
        execution_id,
        task.mutable_execution_id());

    serialization::ToProto(
        submission.tenant_id,
        task.mutable_tenant_id());

    serialization::ToProto(
        submission.agent_id,
        task.mutable_agent_id());

    task.set_attempt(1);

    task.set_name(
        submission.name);

    task.set_entrypoint(
        submission.entrypoint);

    serialization::ToProto(
        submission.resources.resources(),
        task.mutable_resources());

    task.set_payload(
        submission.payload);

    task.set_payload_media_type(
        submission.payload_media_type);

    // --------------------------------------------------------
    // 4. Dispatch to selected node
    // --------------------------------------------------------

    DistributedTaskRecord record;

    record.task_id = task_id;
    record.execution_id = execution_id;
    record.node_id = selected_node.node_id;
    record.resources = submission.resources;
    record.state = DistributedTaskState::KRunning;

    {
        std::lock_guard<std::mutex> lock(tasks_mu_);

        tasks_.emplace(task_id,std::move(record));
    }

    status =
        node_manager_.DispatchTask(
            selected_node,
            task,
            std::chrono::duration_cast<
                std::chrono::milliseconds>(
                    kLaunchRpcTimeout));

    // rollback
    if(!status.ok())
    {
        // rollback for task_manager record
        {
            std::lock_guard<std::mutex> lock(tasks_mu_);

            tasks_.erase(task_id);
        }
        // Worker never accepted the task.
        //
        // Restore cluster reservation.
        const Status release_status =
            registry_.Release(
                selected_node.node_id,
                submission.resources);

        if(!release_status.ok())
        {
            return Status::Internal(
                "task dispatch failed and cluster "
                "resource rollback also failed");
        }

        return status;
    }

    // --------------------------------------------------------
    // 5. Admission succeeded
    // --------------------------------------------------------

    out->task_id = task_id;
    out->execution_id = execution_id;
    out->node_id = selected_node.node_id;

    return Status::OK();
}


// changes task state, RELEASES cluster resources (what stage 4 was missing)
/**
CPU = 1

Submit
  ↓
schedulable = 0

execute
  ↓
ReportTaskResult
  ↓
Release
  ↓
schedulable = 1
 */
// Changed for Phase 3 Stage 5C
/**
First report
    │
    ├── find T1
    ├── verify E1
    ├── verify terminal state
    │
    ├── record already terminal?
    │       │
    │       ├── identical → OK, STOP
    │       └── conflicting → FailedPrecondition, STOP
    │
    ├── Release(record.node_id, record.resources)
    │       │
    │       └── failure → return failure, DON'T terminalize
    │
    ├── commit terminal result
    └── OK
 */
Status ControlPlane::ReportTaskResult(
    TaskID task_id,
    ExecutionID execution_id,
    DistributedTaskState state,
    Status terminal_status,
    std::string result,
    std::string result_media_type)
{
    if(!IsTerminal(state))
    {
        return Status::InvalidArgument(
            "reported task result must be terminal");
    }

    std::lock_guard<std::mutex> lock(tasks_mu_);

    const auto it =
        tasks_.find(task_id);

    if(it == tasks_.end())
    {
        return Status::NotFound(
            "distributed task is not registered");
    }

    DistributedTaskRecord& record =
        it->second;

    if(record.execution_id != execution_id)
    {
        return Status::FailedPrecondition(
            "execution does not match current task execution");
    }

    // --------------------------------------------------------
    // Duplicate terminal report
    // --------------------------------------------------------

    if(IsTerminal(record.state))
    {
        const bool same_completion =
            record.state == state &&
            SameStatus(
                record.terminal_status,
                terminal_status) &&
            record.result == result &&
            record.result_media_type ==
                result_media_type;

        if(!same_completion)
        {
            return Status::FailedPrecondition(
                "conflicting terminal result for "
                "completed execution");
        }

        return Status::OK();
    }

    // --------------------------------------------------------
    // FIRST terminal report
    //
    // Release the reservation while we still have the exact
    // node/resources stored in the record.
    // --------------------------------------------------------

    const Status release_status =
        registry_.Release(
            record.node_id,
            record.resources);

    if(!release_status.ok())
    {
        return release_status;
    }

    // --------------------------------------------------------
    // Only commit terminal state AFTER successful release.
    // --------------------------------------------------------

    record.state =
        state;

    record.terminal_status =
        std::move(terminal_status);

    record.result =
        std::move(result);

    record.result_media_type =
        std::move(result_media_type);

    return Status::OK();
}

Status ControlPlane::GetTaskRecord(
    TaskID task_id,
    DistributedTaskRecord* out
) const
{
    if(out==nullptr)
    {
        return Status::InvalidArgument("task record output must not be null");
    }
    std::lock_guard<std::mutex> lock(tasks_mu_);
    
    const auto it = tasks_.find(task_id);

    if(it == tasks_.end())
    {
        return Status::NotFound("Distributed task is not registered");
    }

    *out = it->second;

    return Status::OK();
}
}  // namespace dar
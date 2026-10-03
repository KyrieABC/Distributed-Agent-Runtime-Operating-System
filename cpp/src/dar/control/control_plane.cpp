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

}


ControlPlane::ControlPlane(
    NodeRegistry& registry,
    NodeManager& node_manager)
    : registry_(registry),
      node_manager_(node_manager)
{
}


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

}  // namespace dar
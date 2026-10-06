#include "dar/control/control_service.h"

#include "dar/serialization/serialization.h"

namespace dar
{
namespace
{

DistributedTaskState FromProtoTaskState(
    proto::v1::TaskState state)
{
    switch(state)
    {
        case proto::v1::TASK_STATE_SUCCEEDED:
            return DistributedTaskState::KSucceeded;

        case proto::v1::TASK_STATE_FAILED:
            return DistributedTaskState::KFailed;

        case proto::v1::TASK_STATE_CANCELLED:
            return DistributedTaskState::KCanceled;

        default:
            return DistributedTaskState::KRunning;
    }
}

}  // namespace


ControlServiceImpl::ControlServiceImpl(
    ControlPlane& control_plane)
    : control_plane_(control_plane)
{
}

grpc::Status ControlServiceImpl::RegisterNode(
    grpc::ServerContext*,
    const proto::v1::RegisterNodeRequest* request,
    proto::v1::RegisterNodeResponse* response)
{
    if(request == nullptr || response == nullptr)
    {
        return grpc::Status(
            grpc::StatusCode::INTERNAL,
            "gRPC supplied null request/response");
    }

    NodeID node_id;

    Status status =
        serialization::FromProto(
            request->node().node_id(),
            &node_id);

    if(!status.ok())
    {
        serialization::ToProto(
            status,
            response->mutable_status());

        return grpc::Status::OK;
    }

    ResourceSet total_resources;

    status =
        serialization::FromProto(
            request->node().total_resources(),
            &total_resources);

    if(!status.ok())
    {
        serialization::ToProto(
            status,
            response->mutable_status());

        return grpc::Status::OK;
    }

    ResourceSet reported_available;

    status =
        serialization::FromProto(
            request->node()
                .reported_available_resources(),
            &reported_available);

    if(!status.ok())
    {
        serialization::ToProto(
            status,
            response->mutable_status());

        return grpc::Status::OK;
    }

    NodeRecord node;

    node.node_id =
        node_id;

    node.worker_endpoint =
        request->node().worker_endpoint();

    node.total_resources =
        std::move(total_resources);

    node.reported_available =
        std::move(reported_available);

    node.incarnation =
        request->node().incarnation();

    // Registration creates a live worker lifetime.
    //
    // Heartbeat-driven SUSPECT/DEAD transitions come later.
    node.state =
        NodeState::kAlive;

    status =
        control_plane_.RegisterNode(
            std::move(node));

    serialization::ToProto(
        status,
        response->mutable_status());

    return grpc::Status::OK;
}


grpc::Status ControlServiceImpl::RemoveNode(
    grpc::ServerContext*,
    const proto::v1::RemoveNodeRequest* request,
    proto::v1::RemoveNodeResponse* response)
{
    if(request == nullptr || response == nullptr)
    {
        return grpc::Status(
            grpc::StatusCode::INTERNAL,
            "gRPC supplied null request/response");
    }

    NodeID node_id;

    Status status =
        serialization::FromProto(
            request->node_id(),
            &node_id);

    if(!status.ok())
    {
        serialization::ToProto(
            status,
            response->mutable_status());

        return grpc::Status::OK;
    }

    status =
        control_plane_.RemoveNode(
            node_id,
            request->incarnation());

    serialization::ToProto(
        status,
        response->mutable_status());

    return grpc::Status::OK;
}

grpc::Status ControlServiceImpl::ReportTaskResult(
    grpc::ServerContext*,
    const proto::v1::ReportTaskResultRequest* request,
    proto::v1::ReportTaskResultResponse* response)
{
    if(request == nullptr || response == nullptr)
    {
        return grpc::Status(
            grpc::StatusCode::INTERNAL,
            "gRPC supplied null request/response");
    }

    TaskID task_id;

    Status status =
        serialization::FromProto(
            request->task_id(),
            &task_id);

    if(!status.ok())
    {
        serialization::ToProto(
            status,
            response->mutable_status());

        return grpc::Status::OK;
    }

    ExecutionID execution_id;

    status =
        serialization::FromProto(
            request->execution_id(),
            &execution_id);

    if(!status.ok())
    {
        serialization::ToProto(
            status,
            response->mutable_status());

        return grpc::Status::OK;
    }

    const Status terminal_status =
        serialization::FromProtoStatus(
            request->status());

    status =
        control_plane_.ReportTaskResult(
            task_id,
            execution_id,
            FromProtoTaskState(
                request->terminal_state()),
            terminal_status,
            request->result(),
            request->result_media_type());

    serialization::ToProto(
        status,
        response->mutable_status());

    return grpc::Status::OK;
}

}  // namespace dar
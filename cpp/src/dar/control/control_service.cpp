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
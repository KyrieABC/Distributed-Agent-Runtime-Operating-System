#include "dar/worker/result_reporter.h"

#include <utility>

#include "dar/serialization/serialization.h"

#include "control.grpc.pb.h"

namespace dar
{
namespace
{

proto::v1::TaskState ToProtoTaskState(
    ExecutionState state)
{
    switch(state)
    {
        case ExecutionState::kSSucceeded:
            return proto::v1::TASK_STATE_SUCCEEDED;

        case ExecutionState::kFailed:
            return proto::v1::TASK_STATE_FAILED;

        case ExecutionState::kCancelled:
            return proto::v1::TASK_STATE_CANCELLED;

        default:
            return proto::v1::TASK_STATE_UNSPECIFIED;
    }
}

}  // namespace


ResultReporter::ResultReporter(
    RpcClient& rpc_client,
    std::string control_endpoint)
    : rpc_client_(rpc_client),
      control_endpoint_(
          std::move(control_endpoint))
{
}


Status ResultReporter::Report(
    TaskID task_id,
    ExecutionID execution_id,
    ExecutionState state,
    const Status& terminal_status,
    const std::string& result,
    std::chrono::milliseconds timeout)
{
    auto channel =
        rpc_client_.Channel(
            control_endpoint_);

    auto stub =
        proto::v1::ControlService::NewStub(
            channel);

    proto::v1::ReportTaskResultRequest request;

    serialization::ToProto(
        task_id,
        request.mutable_task_id());

    serialization::ToProto(
        execution_id,
        request.mutable_execution_id());

    request.set_terminal_state(
        ToProtoTaskState(state));

    serialization::ToProto(
        terminal_status,
        request.mutable_status());

    request.set_result(result);

    request.set_result_media_type(
        "text/plain");

    proto::v1::ReportTaskResultResponse response;

    auto context =
        rpc_client_.CreateContext(timeout);

    const grpc::Status transport_status =
        stub->ReportTaskResult(
            context.get(),
            request,
            &response);

    if(!transport_status.ok())
    {
        return Status::Unavailable(
            "ReportTaskResult RPC failed: " +
            transport_status.error_message());
    }

    return serialization::FromProtoStatus(
        response.status());
}

}  // namespace dar
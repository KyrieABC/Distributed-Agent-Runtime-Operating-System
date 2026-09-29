#include "dar/worker/worker_service.h"

#include <string>
#include <utility>

#include "dar/serialization/serialization.h"

namespace dar
{
namespace
{

grpc::Status ApplicationFailure(
    const Status& status,
    proto::v1::LaunchTaskResponse* response)
{
    ToProto(status, response->mutable_status());

    // Transport succeeded.
    //
    // The DAR operation itself failed, but the RPC was successfully
    // delivered, decoded, processed, and answered.
    return grpc::Status::OK;
}

}  // namespace

WorkerServiceImpl::WorkerServiceImpl(
    Runtime& runtime,
    HandlerRegistry& registry)
    : runtime_(runtime),
      registry_(registry)
{
}

grpc::Status WorkerServiceImpl::LaunchTask(
    grpc::ServerContext*,
    const proto::v1::LaunchTaskRequest* request,
    proto::v1::LaunchTaskResponse* response)
{
    if(request == nullptr || response == nullptr)
    {
        return grpc::Status(
            grpc::StatusCode::INTERNAL,
            "gRPC supplied null request/response");
    }

    const proto::v1::TaskDescriptor& wire_task =
        request->task();

    // --------------------------------------------------------
    // 1. Decode distributed identity
    // --------------------------------------------------------

    TaskID task_id;
    Status status = FromProto(
        wire_task.task_id(),
        &task_id);

    if(!status.ok())
    {
        return ApplicationFailure(status, response);
    }

    TenantID tenant_id;
    status = FromProto(
        wire_task.tenant_id(),
        &tenant_id);

    if(!status.ok())
    {
        return ApplicationFailure(status, response);
    }

    AgentID agent_id;
    status = FromProto(
        wire_task.agent_id(),
        &agent_id);

    if(!status.ok())
    {
        return ApplicationFailure(status, response);
    }

    // Validate the remote execution identity even though Phase 1
    // currently creates its own local execution attempt when the
    // scheduler performs placement.
    ExecutionID remote_execution_id;
    status = FromProto(
        wire_task.execution_id(),
        &remote_execution_id);

    if(!status.ok())
    {
        return ApplicationFailure(status, response);
    }

    if(wire_task.attempt() == 0)
    {
        return ApplicationFailure(
            Status::InvalidArgument(
                "task attempt must be greater than zero"),
            response);
    }

    // --------------------------------------------------------
    // 2. Decode resources
    // --------------------------------------------------------

    ResourceSet resources;

    status = FromProto(
        wire_task.resources(),
        &resources);

    if(!status.ok())
    {
        return ApplicationFailure(status, response);
    }

    // --------------------------------------------------------
    // 3. Resolve distributed code identity
    // --------------------------------------------------------

    if(wire_task.entrypoint().empty())
    {
        return ApplicationFailure(
            Status::InvalidArgument(
                "task entrypoint cannot be empty"),
            response);
    }

    RegisteredHandler registered_handler;

    status = registry_.Resolve(
        wire_task.entrypoint(),
        &registered_handler);

    if(!status.ok())
    {
        return ApplicationFailure(status, response);
    }

    // --------------------------------------------------------
    // 4. Construct the existing Phase-1 TaskSpec
    // --------------------------------------------------------

    TaskSpec spec;

    spec.tenant_id = tenant_id;
    spec.id = task_id;
    spec.agent_id = agent_id;
    spec.name = wire_task.name();

    // ResourceRequest wraps ResourceSet in your Phase-1 model.
    spec.resources.resources = std::move(resources);

    // Phase 3 does not yet implement distributed ObjectRef inputs.
    spec.inputs.clear();

    // --------------------------------------------------------
    // 5. Bind wire payload -> local Phase-1 TaskHandler
    // --------------------------------------------------------

    const std::string payload = wire_task.payload();
    const std::string payload_media_type =
        wire_task.payload_media_type();

    TaskHandler local_handler =
        [
            handler = std::move(registered_handler),
            payload,
            payload_media_type
        ](
            const TaskSpec& local_spec,
            RuntimeContext& context,
            std::string* result) mutable -> Status
        {
            return handler(
                local_spec,
                context,
                payload,
                payload_media_type,
                result);
        };

    // --------------------------------------------------------
    // 6. CROSS THE ARCHITECTURAL BOUNDARY
    // --------------------------------------------------------

    status = runtime_.Submit(
        std::move(spec),
        std::move(local_handler));

    ToProto(status, response->mutable_status());

    // LaunchTask is admission, not task completion.
    //
    // If Runtime::Submit() returned OK, the Phase-1 runtime now owns
    // the task and its scheduler/worker machinery proceeds normally.
    return grpc::Status::OK;
}

}  // namespace dar
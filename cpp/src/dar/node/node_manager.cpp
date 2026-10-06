#include "dar/node/node_manager.h"

#include "dar/serialization/serialization.h"

#include "worker.grpc.pb.h"

namespace dar
{

    // NodeRegistry: StoreNode (struct, contain 1 NodeRecord-all information about a node), schedulable_pool_(ResourceSet)
    // rpc_client: connection_pool(contains: A map of endpoints to gRPC channels)
NodeManager::NodeManager(
    NodeRegistry& registry,
    RpcClient& rpc_client)
    : registry_(registry),
      rpc_client_(rpc_client)
{
}


Status NodeManager::RegisterNode(NodeRecord node)
{
    return registry_.RegisterNode(
        std::move(node));
}

/**
 * ownership pattern established in 5D
ControlPlane
    ↓
NodeManager
    ↓
NodeRegistry
 */
Status NodeManager::Heartbeat(
    NodeID node_id,
    std::uint64_t incarnation,
    const ResourceSet& reported_available,
    std::uint32_t running_tasks)
{
    return registry_.Heartbeat(
        node_id,
        incarnation,
        reported_available,
        running_tasks);
}

Status NodeManager::DispatchTask(
    const NodeRecord& node,
    const proto::v1::TaskDescriptor& task,
    std::chrono::milliseconds timeout)
{
    if(timeout <=
       std::chrono::milliseconds::zero())
    {
        return Status::InvalidArgument(
            "LaunchTask RPC timeout must be greater than zero");
    }

    auto channel =
        rpc_client_.Channel(
            node.worker_endpoint);

    auto stub =
        proto::v1::WorkerService::NewStub(
            channel);

    proto::v1::LaunchTaskRequest request;

    *request.mutable_task() =
        task;

    // --------------------------------------------------------
    // Phase 3 Stage 5I:
    //
    // Transport redelivery.
    //
    // Every attempt sends the EXACT SAME TaskDescriptor,
    // therefore:
    //
    //     same TaskID
    //     same ExecutionID
    //     same attempt
    //
    // WorkerService's ExecutionID ledger makes this safe.
    //
    // This is NOT semantic retry.
    // --------------------------------------------------------

    grpc::Status last_transport_status;

    for(std::size_t attempt = 0;
        attempt < kMaxLaunchAttempts;
        ++attempt)
    {
        proto::v1::LaunchTaskResponse response;

        // A ClientContext belongs to exactly one RPC.
        //
        // Never reuse the context from a previous attempt.
        auto context =
            rpc_client_.CreateContext(
                timeout);

        const grpc::Status transport_status =
            stub->LaunchTask(
                context.get(),
                request,
                &response);

        // ----------------------------------------------------
        // Transport succeeded.
        //
        // Whatever DAR status the worker returned is
        // authoritative. Do NOT retry application failures.
        // ----------------------------------------------------

        if(transport_status.ok())
        {
            return serialization::FromProtoStatus(
                response.status());
        }

        last_transport_status =
            transport_status;

        // ----------------------------------------------------
        // Only ambiguous/recoverable transport failures are
        // redelivered.
        //
        // We deliberately keep this list small in Phase 3.
        // ----------------------------------------------------

        const grpc::StatusCode code =
            transport_status.error_code();

        const bool retryable =
            code == grpc::StatusCode::UNAVAILABLE ||
            code == grpc::StatusCode::DEADLINE_EXCEEDED;

        if(!retryable)
        {
            return Status::Unavailable(
                "LaunchTask RPC failed: " +
                transport_status.error_message());
        }

        // No more attempts remain.
        if(attempt + 1 >=
           kMaxLaunchAttempts)
        {
            break;
        }

        // Intentionally no sophisticated backoff in Phase 3.
        //
        // One immediate bounded redelivery is enough to prove
        // the at-least-once + idempotency architecture.
    }

    return Status::Unavailable(
        "LaunchTask RPC failed after transport redelivery: " +
        last_transport_status.error_message());
}

Status NodeManager::CancelTask(
    const NodeRecord& node,
    TaskID task_id,
    ExecutionID execution_id,
    std::chrono::milliseconds timeout)
{
    if(task_id.IsNil())
    {
        return Status::InvalidArgument(
            "cancel task id cannot be nil");
    }

    if(execution_id.IsNil())
    {
        return Status::InvalidArgument(
            "cancel execution id cannot be nil");
    }

    auto channel =
        rpc_client_.Channel(
            node.worker_endpoint);

    auto stub =
        proto::v1::WorkerService::NewStub(
            channel);

    proto::v1::CancelWorkerTaskRequest request;

    serialization::ToProto(
        task_id,
        request.mutable_task_id());

    serialization::ToProto(
        execution_id,
        request.mutable_execution_id());

    proto::v1::CancelWorkerTaskResponse response;

    auto context =
        rpc_client_.CreateContext(
            timeout);

    const grpc::Status transport_status =
        stub->CancelTask(
            context.get(),
            request,
            &response);

    if(!transport_status.ok())
    {
        return Status::Unavailable(
            "CancelTask RPC failed: " +
            transport_status.error_message());
    }

    return serialization::FromProtoStatus(
        response.status());
}

}  // namespace dar
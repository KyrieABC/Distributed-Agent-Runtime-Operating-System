#include <chrono>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "dar/rpc/connection_pool.h"
#include "dar/rpc/rpc_client.h"
#include "dar/rpc/rpc_server.h"
#include "dar/runtime/runtime.h"
#include "dar/serialization/serialization.h"
#include "dar/worker/builtin_handlers.h"
#include "dar/worker/handler_registry.h"
#include "dar/worker/worker_service.h"

#include "worker.grpc.pb.h"

namespace dar
{
namespace
{

TEST(
    Phase3WorkerServiceTest,
    LaunchTaskCrossesNetworkIntoPhase1Runtime)
{
    // --------------------------------------------------------
    // Phase-1 runtime
    // --------------------------------------------------------

    RuntimeOptions options;
    options.worker_count = 1;

    ASSERT_TRUE(
        options.resources_per_worker.Set(
            "CPU",
            1.0).ok());

    Runtime runtime(std::move(options));

    ASSERT_TRUE(runtime.Start().ok());

    // --------------------------------------------------------
    // Worker-side code registry
    // --------------------------------------------------------

    HandlerRegistry registry;

    ASSERT_TRUE(
        RegisterBuiltinHandlers(registry).ok());

    ASSERT_TRUE(
        registry.Contains("builtin.square"));

    // --------------------------------------------------------
    // Production WorkerService
    // --------------------------------------------------------

    WorkerServiceImpl worker_service(
        runtime,
        registry);

    RpcServer server("127.0.0.1:0");

    ASSERT_TRUE(
        server.RegisterService(&worker_service).ok());

    ASSERT_TRUE(server.Start().ok());

    ASSERT_GT(server.SelectedPort(), 0);

    const std::string endpoint =
        "127.0.0.1:" +
        std::to_string(server.SelectedPort());

    // --------------------------------------------------------
    // Remote client
    // --------------------------------------------------------

    ConnectionPool connection_pool;
    RpcClient rpc_client(connection_pool);

    auto channel =
        rpc_client.Channel(endpoint);

    auto stub =
        proto::v1::WorkerService::NewStub(channel);

    // --------------------------------------------------------
    // Construct distributed TaskDescriptor
    // --------------------------------------------------------

    const TenantID tenant_id = TenantID::Random();
    const AgentID agent_id = AgentID::Random();
    const TaskID task_id = TaskID::Random();
    const ExecutionID execution_id =
        ExecutionID::Random();

    proto::v1::LaunchTaskRequest request;

    auto* task = request.mutable_task();

    serialization::ToProto(
        tenant_id,
        task->mutable_tenant_id());

    serialization::ToProto(
        agent_id,
        task->mutable_agent_id());

    serialization::ToProto(
        task_id,
        task->mutable_task_id());

    serialization::ToProto(
        execution_id,
        task->mutable_execution_id());

    task->set_attempt(1);

    task->set_name("square-12");

    task->set_entrypoint(
        "builtin.square");

    task->set_payload("12");

    task->set_payload_media_type(
        "text/plain");

    (*task->mutable_resources()
          ->mutable_quantities())["CPU"] = 1.0;

    // --------------------------------------------------------
    // CROSS THE REAL gRPC BOUNDARY
    // --------------------------------------------------------

    proto::v1::LaunchTaskResponse response;

    auto context =
        rpc_client.CreateContext(
            std::chrono::seconds(2));

    const grpc::Status transport_status =
        stub->LaunchTask(
            context.get(),
            request,
            &response);

    // Transport succeeded.
    ASSERT_TRUE(transport_status.ok())
        << transport_status.error_message();

    // Runtime admission succeeded.
    const Status launch_status =
        serialization::FromProtoStatus(response.status());

    ASSERT_TRUE(launch_status.ok())
        << launch_status.ToString();

    // --------------------------------------------------------
    // Prove execution actually went through Phase 1
    // --------------------------------------------------------

    std::string result;

    const Status result_status =
        runtime.GetResult(
            task_id,
            &result,
            std::chrono::seconds(2));

    ASSERT_TRUE(result_status.ok())
        << result_status.ToString();

    EXPECT_EQ(result, "144");

    ASSERT_TRUE(server.Shutdown().ok());
    ASSERT_TRUE(runtime.Shutdown().ok());
}

}  // namespace
}  // namespace dar
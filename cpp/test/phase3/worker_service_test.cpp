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
    DuplicateExecutionIDIsSubmittedOnlyOnce)
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
    // Handler whose execution count is observable
    // --------------------------------------------------------

    HandlerRegistry registry;

    std::atomic<int> execution_count{0};

    ASSERT_TRUE(
        registry.Register(
            "test.count_once",
            [&execution_count](
                const TaskSpec&,
                RuntimeContext&,
                std::string_view,
                std::string_view,
                std::string* result) -> Status
            {
                ++execution_count;

                if(result != nullptr)
                {
                    *result = "executed";
                }

                return Status::OK();
            }).ok());

    // --------------------------------------------------------
    // WorkerService
    // --------------------------------------------------------

    WorkerServiceImpl worker_service(
        runtime,
        registry);

    RpcServer server("127.0.0.1:0");

    ASSERT_TRUE(
        server.RegisterService(
            &worker_service).ok());

    ASSERT_TRUE(server.Start().ok());

    ASSERT_GT(server.SelectedPort(), 0);

    const std::string endpoint =
        "127.0.0.1:" +
        std::to_string(
            server.SelectedPort());

    // --------------------------------------------------------
    // Client
    // --------------------------------------------------------

    ConnectionPool connection_pool;
    RpcClient rpc_client(connection_pool);

    auto channel =
        rpc_client.Channel(endpoint);

    auto stub =
        proto::v1::WorkerService::NewStub(
            channel);

    // --------------------------------------------------------
    // Construct one distributed execution: T1 / E1
    // --------------------------------------------------------

    const TenantID tenant_id =
        TenantID::Random();

    const AgentID agent_id =
        AgentID::Random();

    const TaskID task_id =
        TaskID::Random();

    const ExecutionID execution_id =
        ExecutionID::Random();

    proto::v1::LaunchTaskRequest request;

    auto* task =
        request.mutable_task();

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

    task->set_name(
        "duplicate-execution-test");

    task->set_entrypoint(
        "test.count_once");

    task->set_payload("");

    task->set_payload_media_type(
        "text/plain");

    (*task->mutable_resources()
          ->mutable_quantities())["CPU"] = 1.0;

    // --------------------------------------------------------
    // First delivery of E1
    // --------------------------------------------------------

    proto::v1::LaunchTaskResponse first_response;

    auto first_context =
        rpc_client.CreateContext(
            std::chrono::seconds(2));

    const grpc::Status first_transport =
        stub->LaunchTask(
            first_context.get(),
            request,
            &first_response);

    ASSERT_TRUE(first_transport.ok())
        << first_transport.error_message();

    const Status first_status =
        serialization::FromProtoStatus(
            first_response.status());

    ASSERT_TRUE(first_status.ok())
        << first_status.ToString();

    // --------------------------------------------------------
    // Wait for the original execution to finish.
    //
    // This is intentional:
    // we want to prove E1 is remembered even AFTER completion.
    // --------------------------------------------------------

    std::string result;

    const Status result_status =
        runtime.GetResult(
            task_id,
            &result,
            std::chrono::seconds(2));

    ASSERT_TRUE(result_status.ok())
        << result_status.ToString();

    EXPECT_EQ(result, "executed");

    EXPECT_EQ(
        execution_count.load(),
        1);

    // --------------------------------------------------------
    // Deliver EXACTLY the same distributed execution again.
    //
    // Same TaskID.
    // Same ExecutionID.
    // Same attempt.
    // --------------------------------------------------------

    proto::v1::LaunchTaskResponse duplicate_response;

    auto duplicate_context =
        rpc_client.CreateContext(
            std::chrono::seconds(2));

    const grpc::Status duplicate_transport =
        stub->LaunchTask(
            duplicate_context.get(),
            request,
            &duplicate_response);

    ASSERT_TRUE(duplicate_transport.ok())
        << duplicate_transport.error_message();

    const Status duplicate_status =
        serialization::FromProtoStatus(
            duplicate_response.status());

    // Duplicate delivery is idempotent, not an application error.
    EXPECT_TRUE(duplicate_status.ok())
        << duplicate_status.ToString();

    // --------------------------------------------------------
    // Critical Stage-5B invariant
    // --------------------------------------------------------

    EXPECT_EQ(
        execution_count.load(),
        1);

    ASSERT_TRUE(server.Shutdown().ok());
    ASSERT_TRUE(runtime.Shutdown().ok());
}

}  // namespace
}  // namespace dar
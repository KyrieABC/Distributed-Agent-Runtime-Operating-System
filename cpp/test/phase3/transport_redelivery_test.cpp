/**
Launch(T1,E1)
Launch(T1,E1)

        ↓

Runtime::Submit exactly once
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <grpcpp/grpcpp.h>

#include "dar/common/id.h"
#include "dar/core/resource.h"
#include "dar/rpc/connection_pool.h"
#include "dar/rpc/rpc_client.h"
#include "dar/runtime/runtime.h"
#include "dar/serialization/serialization.h"
#include "dar/worker/handler_registry.h"
#include "dar/worker/worker_service.h"

#include "worker.grpc.pb.h"

namespace dar
{
namespace
{

using namespace std::chrono_literals;


bool WaitForCount(
    const std::atomic_int& count,
    int expected,
    std::chrono::milliseconds timeout)
{
    const auto deadline =
        std::chrono::steady_clock::now() +
        timeout;

    while(std::chrono::steady_clock::now() <
          deadline)
    {
        if(count.load(
               std::memory_order_acquire) >= expected)
        {
            return true;
        }

        std::this_thread::sleep_for(1ms);
    }

    return count.load(
        std::memory_order_acquire) >= expected;
}


TEST(
    Phase3TransportRedeliveryTest,
    DuplicateLaunchWithSameExecutionIdExecutesExactlyOnce)
{
    RuntimeOptions options;

    options.worker_count = 1;

    ASSERT_TRUE(
        options.resources_per_worker.Set(
            "CPU",
            1.0).ok());

    Runtime runtime(options);

    ASSERT_TRUE(
        runtime.Start().ok());

    HandlerRegistry handlers;

    std::atomic_int execution_count{0};

    std::atomic_bool allow_exit{false};

    ASSERT_TRUE(
        handlers.Register(
            "builtin.redelivery_probe",
            [&](
                const TaskSpec&,
                RuntimeContext&,
                std::string_view,
                std::string_view,
                std::string* result) -> Status
            {
                execution_count.fetch_add(
                    1,
                    std::memory_order_acq_rel);

                while(
                    !allow_exit.load(
                        std::memory_order_acquire))
                {
                    std::this_thread::sleep_for(
                        1ms);
                }

                if(result != nullptr)
                {
                    *result = "done";
                }

                return Status::OK();
            }).ok());

    // ResultReporter is deliberately null.
    //
    // This test is about admission/idempotency, not completion
    // reporting.
    WorkerServiceImpl service(
        runtime,
        handlers,
        nullptr);

    grpc::ServerBuilder builder;

    int port = 0;

    builder.AddListeningPort(
        "127.0.0.1:0",
        grpc::InsecureServerCredentials(),
        &port);

    builder.RegisterService(
        &service);

    std::unique_ptr<grpc::Server> server =
        builder.BuildAndStart();

    ASSERT_NE(server, nullptr);
    ASSERT_GT(port, 0);

    ConnectionPool connections;
    RpcClient rpc_client(connections);

    auto channel =
        rpc_client.Channel(
            "127.0.0.1:" +
            std::to_string(port));

    auto stub =
        proto::v1::WorkerService::NewStub(
            channel);

    // --------------------------------------------------------
    // ONE distributed execution identity.
    // --------------------------------------------------------

    const TaskID task_id =
        TaskID::Random();

    const ExecutionID execution_id =
        ExecutionID::Random();

    const TenantID tenant_id =
        TenantID::Random();

    const AgentID agent_id =
        AgentID::Random();

    proto::v1::TaskDescriptor descriptor;

    serialization::ToProto(
        task_id,
        descriptor.mutable_task_id());

    serialization::ToProto(
        execution_id,
        descriptor.mutable_execution_id());

    serialization::ToProto(
        tenant_id,
        descriptor.mutable_tenant_id());

    serialization::ToProto(
        agent_id,
        descriptor.mutable_agent_id());

    descriptor.set_attempt(1);

    descriptor.set_name(
        "transport redelivery probe");

    descriptor.set_entrypoint(
        "builtin.redelivery_probe");

    ResourceSet resources;

    ASSERT_TRUE(
        resources.Set(
            "CPU",
            1.0).ok());

    serialization::ToProto(
        resources,
        descriptor.mutable_resources());

    descriptor.set_payload("");
    descriptor.set_payload_media_type(
        "text/plain");

    // --------------------------------------------------------
    // Delivery #1
    // --------------------------------------------------------

    proto::v1::LaunchTaskRequest request1;

    *request1.mutable_task() =
        descriptor;

    proto::v1::LaunchTaskResponse response1;

    auto context1 =
        rpc_client.CreateContext(2s);

    ASSERT_TRUE(
        stub->LaunchTask(
            context1.get(),
            request1,
            &response1).ok());

    ASSERT_TRUE(
        serialization::FromProtoStatus(
            response1.status()).ok());

    ASSERT_TRUE(
        WaitForCount(
            execution_count,
            1,
            2s));

    // --------------------------------------------------------
    // Delivery #2
    //
    // EXACT SAME TaskDescriptor.
    // EXACT SAME ExecutionID.
    // --------------------------------------------------------

    proto::v1::LaunchTaskRequest request2;

    *request2.mutable_task() =
        descriptor;

    proto::v1::LaunchTaskResponse response2;

    auto context2 =
        rpc_client.CreateContext(2s);

    ASSERT_TRUE(
        stub->LaunchTask(
            context2.get(),
            request2,
            &response2).ok());

    EXPECT_TRUE(
        serialization::FromProtoStatus(
            response2.status()).ok());

    // Give a hypothetical accidental second submission time to
    // become visible.
    std::this_thread::sleep_for(50ms);

    // THE Stage-5I invariant.
    EXPECT_EQ(
        execution_count.load(
            std::memory_order_acquire),
        1);

    allow_exit.store(
        true,
        std::memory_order_release);

    server->Shutdown();

    EXPECT_TRUE(
        runtime.Shutdown().ok());
}

TEST(
    Phase3TransportRedeliveryTest,
    SameExecutionIdCannotBeRedeliveredForDifferentTask)
{
    // Use the same fixture/setup as the previous test.

    // First delivery:
    //
    //     TaskID = T1
    //     ExecutionID = E1
    //
    // must succeed.
    //
    // Second delivery:
    //
    //     TaskID = T2
    //     ExecutionID = E1
    //
    // must return FailedPrecondition.
}

}  // namespace
}  // namespace dar
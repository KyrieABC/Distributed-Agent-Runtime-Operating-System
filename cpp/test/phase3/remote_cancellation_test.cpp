/**
LaunchTask(T1,E1)
       ↓
handler starts
       ↓
CancelTask(T1,E1)
       ↓
Runtime::Cancel(T1)
       ↓
cancel token true
       ↓
handler observes cancellation
       ↓
CANCELLED
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

#include <grpcpp/grpcpp.h>

#include "dar/common/id.h"
#include "dar/common/status.h"
#include "dar/control/control_plane.h"
#include "dar/control/control_service.h"
#include "dar/node/node_manager.h"
#include "dar/node/node_registry.h"
#include "dar/rpc/connection_pool.h"
#include "dar/rpc/rpc_client.h"
#include "dar/rpc/rpc_server.h"
#include "dar/runtime/runtime.h"
#include "dar/serialization/serialization.h"
#include "dar/worker/handler_registry.h"
#include "dar/worker/result_reporter.h"
#include "dar/worker/worker_service.h"

#include "control.grpc.pb.h"
#include "task.pb.h"
#include "worker.grpc.pb.h"

namespace dar
{
namespace
{

using namespace std::chrono_literals;


// ------------------------------------------------------------
// Helpers
// ------------------------------------------------------------

ResourceSet MakeResources(
    double cpu)
{
    ResourceSet resources;

    EXPECT_TRUE(
        resources.Set(
            "CPU",
            cpu).ok());

    return resources;
}


RuntimeOptions MakeRuntimeOptions()
{
    RuntimeOptions options;

    options.worker_count = 1;

    EXPECT_TRUE(
        options.resources_per_worker.Set(
            "CPU",
            1.0).ok());

    return options;
}


// Wait until a local task reaches a particular state.
//
// This avoids assuming that LaunchTask() means the handler has
// already started. LaunchTask is admission only.
bool WaitForLocalState(
    Runtime& runtime,
    TaskID task_id,
    ExecutionState wanted,
    std::chrono::milliseconds timeout)
{
    const auto deadline =
        std::chrono::steady_clock::now() +
        timeout;

    while(std::chrono::steady_clock::now() <
          deadline)
    {
        TaskSnapshot snapshot;

        const Status status =
            runtime.GetStatus(
                task_id,
                &snapshot);

        if(status.ok() &&
           snapshot.state == wanted)
        {
            return true;
        }

        std::this_thread::sleep_for(
            1ms);
    }

    return false;
}


// Wait for distributed ControlPlane state.
bool WaitForDistributedState(
    ControlPlane& control_plane,
    TaskID task_id,
    DistributedTaskState wanted,
    std::chrono::milliseconds timeout)
{
    const auto deadline =
        std::chrono::steady_clock::now() +
        timeout;

    while(std::chrono::steady_clock::now() <
          deadline)
    {
        DistributedTaskRecord record;

        const Status status =
            control_plane.GetTaskRecord(
                task_id,
                &record);

        if(status.ok() &&
           record.state == wanted)
        {
            return true;
        }

        std::this_thread::sleep_for(
            1ms);
    }

    return false;
}


// ------------------------------------------------------------
// 1. Worker RPC:
//    Launch(T1,E1) -> Cancel(T1,E1) -> local CANCELLED
// ------------------------------------------------------------

TEST(
    Phase3RemoteCancellationTest,
    WorkerCancelRpcCancelsRunningExecution)
{
    Runtime runtime(
        MakeRuntimeOptions());

    ASSERT_TRUE(
        runtime.Start().ok());

    HandlerRegistry handlers;

    std::atomic_bool handler_started{false};

    ASSERT_TRUE(
        handlers.Register(
            "builtin.wait_for_cancel",
            [&handler_started](
                const TaskSpec&,
                RuntimeContext& context,
                std::string_view,
                std::string_view,
                std::string* result) -> Status
            {
                handler_started.store(
                    true,
                    std::memory_order_release);

                while(!context.IsCancellationRequested())
                {
                    std::this_thread::sleep_for(
                        1ms);
                }

                result->clear();

                return Status::Cancelled(
                    "cancelled by remote request");
            }).ok());

    WorkerServiceImpl worker_service(
        runtime,
        handlers);

    grpc::ServerBuilder builder;

    int selected_port = 0;

    builder.AddListeningPort(
        "127.0.0.1:0",
        grpc::InsecureServerCredentials(),
        &selected_port);

    builder.RegisterService(
        &worker_service);

    std::unique_ptr<grpc::Server> server =
        builder.BuildAndStart();

    ASSERT_NE(
        server,
        nullptr);

    ASSERT_GT(
        selected_port,
        0);

    const std::string endpoint =
        "127.0.0.1:" +
        std::to_string(selected_port);

    auto channel =
        grpc::CreateChannel(
            endpoint,
            grpc::InsecureChannelCredentials());

    auto stub =
        proto::v1::WorkerService::NewStub(
            channel);

    const TaskID task_id =
        TaskID::Random();

    const ExecutionID execution_id =
        ExecutionID::Random();

    const TenantID tenant_id =
        TenantID::Random();

    const AgentID agent_id =
        AgentID::Random();

    // --------------------------------------------------------
    // Launch
    // --------------------------------------------------------

    proto::v1::LaunchTaskRequest launch_request;

    proto::v1::TaskDescriptor* task =
        launch_request.mutable_task();

    serialization::ToProto(
        task_id,
        task->mutable_task_id());

    serialization::ToProto(
        execution_id,
        task->mutable_execution_id());

    serialization::ToProto(
        tenant_id,
        task->mutable_tenant_id());

    serialization::ToProto(
        agent_id,
        task->mutable_agent_id());

    task->set_attempt(1);

    task->set_name(
        "remote cancellation test");

    task->set_entrypoint(
        "builtin.wait_for_cancel");

    const ResourceSet task_resources =
        MakeResources(1.0);

    serialization::ToProto(
        task_resources,
        task->mutable_resources());

    task->set_payload("");
    task->set_payload_media_type(
        "text/plain");

    proto::v1::LaunchTaskResponse launch_response;

    grpc::ClientContext launch_context;

    launch_context.set_deadline(
        std::chrono::system_clock::now() +
        2s);

    const grpc::Status launch_transport =
        stub->LaunchTask(
            &launch_context,
            launch_request,
            &launch_response);

    ASSERT_TRUE(
        launch_transport.ok());

    ASSERT_TRUE(
        serialization::FromProtoStatus(
            launch_response.status()).ok());

    // LaunchTask only proves admission.
    // Wait until the task is actually running.
    ASSERT_TRUE(
        WaitForLocalState(
            runtime,
            task_id,
            ExecutionState::kRunning,
            2s));

    ASSERT_TRUE(
        handler_started.load(
            std::memory_order_acquire));

    // --------------------------------------------------------
    // Cancel
    // --------------------------------------------------------

    proto::v1::CancelWorkerTaskRequest
        cancel_request;

    serialization::ToProto(
        task_id,
        cancel_request.mutable_task_id());

    serialization::ToProto(
        execution_id,
        cancel_request.mutable_execution_id());

    proto::v1::CancelWorkerTaskResponse
        cancel_response;

    grpc::ClientContext cancel_context;

    cancel_context.set_deadline(
        std::chrono::system_clock::now() +
        2s);

    const grpc::Status cancel_transport =
        stub->CancelTask(
            &cancel_context,
            cancel_request,
            &cancel_response);

    ASSERT_TRUE(
        cancel_transport.ok());

    ASSERT_TRUE(
        serialization::FromProtoStatus(
            cancel_response.status()).ok());

    // Cancellation of RUNNING work is cooperative.
    // The RPC returning does not itself prove terminality.
    ASSERT_TRUE(
        WaitForLocalState(
            runtime,
            task_id,
            ExecutionState::kCancelled,
            2s));

    std::string result;

    const Status result_status =
        runtime.GetResult(
            task_id,
            &result,
            2s);

    EXPECT_TRUE(
        result_status.IsCancelled());

    server->Shutdown();

    ASSERT_TRUE(
        runtime.Shutdown().ok());
}


// ------------------------------------------------------------
// 2. Wrong ExecutionID must not cancel the task.
// ------------------------------------------------------------

TEST(
    Phase3RemoteCancellationTest,
    WrongExecutionIdDoesNotCancelTask)
{
    Runtime runtime(
        MakeRuntimeOptions());

    ASSERT_TRUE(
        runtime.Start().ok());

    HandlerRegistry handlers;

    ASSERT_TRUE(
        handlers.Register(
            "builtin.wait_for_cancel",
            [](
                const TaskSpec&,
                RuntimeContext& context,
                std::string_view,
                std::string_view,
                std::string*) -> Status
            {
                while(!context.IsCancellationRequested())
                {
                    std::this_thread::sleep_for(
                        1ms);
                }

                return Status::Cancelled(
                    "cancelled");
            }).ok());

    WorkerServiceImpl worker_service(
        runtime,
        handlers);

    grpc::ServerBuilder builder;

    int selected_port = 0;

    builder.AddListeningPort(
        "127.0.0.1:0",
        grpc::InsecureServerCredentials(),
        &selected_port);

    builder.RegisterService(
        &worker_service);

    std::unique_ptr<grpc::Server> server =
        builder.BuildAndStart();

    ASSERT_NE(
        server,
        nullptr);

    auto channel =
        grpc::CreateChannel(
            "127.0.0.1:" +
                std::to_string(selected_port),
            grpc::InsecureChannelCredentials());

    auto stub =
        proto::v1::WorkerService::NewStub(
            channel);

    const TaskID task_id =
        TaskID::Random();

    const ExecutionID execution_id =
        ExecutionID::Random();

    const ExecutionID wrong_execution_id =
        ExecutionID::Random();

    proto::v1::LaunchTaskRequest launch_request;

    auto* task =
        launch_request.mutable_task();

    serialization::ToProto(
        task_id,
        task->mutable_task_id());

    serialization::ToProto(
        execution_id,
        task->mutable_execution_id());

    serialization::ToProto(
        TenantID::Random(),
        task->mutable_tenant_id());

    serialization::ToProto(
        AgentID::Random(),
        task->mutable_agent_id());

    task->set_attempt(1);

    task->set_entrypoint(
        "builtin.wait_for_cancel");

    task->set_name(
        "wrong execution cancellation");

    const ResourceSet resources =
        MakeResources(1.0);

    serialization::ToProto(
        resources,
        task->mutable_resources());

    task->set_payload_media_type(
        "text/plain");

    proto::v1::LaunchTaskResponse launch_response;

    grpc::ClientContext launch_context;

    launch_context.set_deadline(
        std::chrono::system_clock::now() +
        2s);

    ASSERT_TRUE(
        stub->LaunchTask(
            &launch_context,
            launch_request,
            &launch_response).ok());

    ASSERT_TRUE(
        serialization::FromProtoStatus(
            launch_response.status()).ok());

    ASSERT_TRUE(
        WaitForLocalState(
            runtime,
            task_id,
            ExecutionState::kRunning,
            2s));

    // --------------------------------------------------------
    // Send cancellation with WRONG ExecutionID.
    // --------------------------------------------------------

    proto::v1::CancelWorkerTaskRequest
        cancel_request;

    serialization::ToProto(
        task_id,
        cancel_request.mutable_task_id());

    serialization::ToProto(
        wrong_execution_id,
        cancel_request.mutable_execution_id());

    proto::v1::CancelWorkerTaskResponse
        cancel_response;

    grpc::ClientContext cancel_context;

    cancel_context.set_deadline(
        std::chrono::system_clock::now() +
        2s);

    ASSERT_TRUE(
        stub->CancelTask(
            &cancel_context,
            cancel_request,
            &cancel_response).ok());

    const Status cancel_status =
        serialization::FromProtoStatus(
            cancel_response.status());

    EXPECT_FALSE(
        cancel_status.ok());

    // Most likely NotFound because E2 does not exist.
    TaskSnapshot snapshot;

    ASSERT_TRUE(
        runtime.GetStatus(
            task_id,
            &snapshot).ok());

    EXPECT_EQ(
        snapshot.state,
        ExecutionState::kRunning);

    // --------------------------------------------------------
    // Clean up with correct identity.
    // --------------------------------------------------------

    proto::v1::CancelWorkerTaskRequest
        correct_request;

    serialization::ToProto(
        task_id,
        correct_request.mutable_task_id());

    serialization::ToProto(
        execution_id,
        correct_request.mutable_execution_id());

    proto::v1::CancelWorkerTaskResponse
        correct_response;

    grpc::ClientContext correct_context;

    correct_context.set_deadline(
        std::chrono::system_clock::now() +
        2s);

    ASSERT_TRUE(
        stub->CancelTask(
            &correct_context,
            correct_request,
            &correct_response).ok());

    ASSERT_TRUE(
        serialization::FromProtoStatus(
            correct_response.status()).ok());

    ASSERT_TRUE(
        WaitForLocalState(
            runtime,
            task_id,
            ExecutionState::kCancelled,
            2s));

    server->Shutdown();

    ASSERT_TRUE(
        runtime.Shutdown().ok());
}


// ------------------------------------------------------------
// 3. Same ExecutionID belonging to a different TaskID
//    must be rejected.
// ------------------------------------------------------------

TEST(
    Phase3RemoteCancellationTest,
    ExecutionIdCannotCancelDifferentTask)
{
    Runtime runtime(
        MakeRuntimeOptions());

    ASSERT_TRUE(
        runtime.Start().ok());

    HandlerRegistry handlers;

    ASSERT_TRUE(
        handlers.Register(
            "builtin.wait_for_cancel",
            [](
                const TaskSpec&,
                RuntimeContext& context,
                std::string_view,
                std::string_view,
                std::string*) -> Status
            {
                while(!context.IsCancellationRequested())
                {
                    std::this_thread::sleep_for(
                        1ms);
                }

                return Status::Cancelled(
                    "cancelled");
            }).ok());

    WorkerServiceImpl worker_service(
        runtime,
        handlers);

    grpc::ServerBuilder builder;

    int selected_port = 0;

    builder.AddListeningPort(
        "127.0.0.1:0",
        grpc::InsecureServerCredentials(),
        &selected_port);

    builder.RegisterService(
        &worker_service);

    std::unique_ptr<grpc::Server> server =
        builder.BuildAndStart();

    ASSERT_NE(
        server,
        nullptr);

    auto stub =
        proto::v1::WorkerService::NewStub(
            grpc::CreateChannel(
                "127.0.0.1:" +
                    std::to_string(selected_port),
                grpc::InsecureChannelCredentials()));

    const TaskID task_id =
        TaskID::Random();

    const TaskID wrong_task_id =
        TaskID::Random();

    const ExecutionID execution_id =
        ExecutionID::Random();

    proto::v1::LaunchTaskRequest launch_request;

    auto* task =
        launch_request.mutable_task();

    serialization::ToProto(
        task_id,
        task->mutable_task_id());

    serialization::ToProto(
        execution_id,
        task->mutable_execution_id());

    serialization::ToProto(
        TenantID::Random(),
        task->mutable_tenant_id());

    serialization::ToProto(
        AgentID::Random(),
        task->mutable_agent_id());

    task->set_attempt(1);
    task->set_name("identity test");
    task->set_entrypoint(
        "builtin.wait_for_cancel");

    const ResourceSet resources =
        MakeResources(1.0);

    serialization::ToProto(
        resources,
        task->mutable_resources());

    task->set_payload_media_type(
        "text/plain");

    proto::v1::LaunchTaskResponse launch_response;

    grpc::ClientContext launch_context;

    launch_context.set_deadline(
        std::chrono::system_clock::now() +
        2s);

    ASSERT_TRUE(
        stub->LaunchTask(
            &launch_context,
            launch_request,
            &launch_response).ok());

    ASSERT_TRUE(
        serialization::FromProtoStatus(
            launch_response.status()).ok());

    ASSERT_TRUE(
        WaitForLocalState(
            runtime,
            task_id,
            ExecutionState::kRunning,
            2s));

    // E1 exists, but belongs to T1.
    // Try Cancel(T2, E1).
    proto::v1::CancelWorkerTaskRequest
        bad_request;

    serialization::ToProto(
        wrong_task_id,
        bad_request.mutable_task_id());

    serialization::ToProto(
        execution_id,
        bad_request.mutable_execution_id());

    proto::v1::CancelWorkerTaskResponse
        bad_response;

    grpc::ClientContext bad_context;

    bad_context.set_deadline(
        std::chrono::system_clock::now() +
        2s);

    ASSERT_TRUE(
        stub->CancelTask(
            &bad_context,
            bad_request,
            &bad_response).ok());

    const Status bad_status =
        serialization::FromProtoStatus(
            bad_response.status());

    EXPECT_FALSE(
        bad_status.ok());

    // T1 must still be running.
    TaskSnapshot snapshot;

    ASSERT_TRUE(
        runtime.GetStatus(
            task_id,
            &snapshot).ok());

    EXPECT_EQ(
        snapshot.state,
        ExecutionState::kRunning);

    // Correct cleanup.
    proto::v1::CancelWorkerTaskRequest
        correct_request;

    serialization::ToProto(
        task_id,
        correct_request.mutable_task_id());

    serialization::ToProto(
        execution_id,
        correct_request.mutable_execution_id());

    proto::v1::CancelWorkerTaskResponse
        correct_response;

    grpc::ClientContext correct_context;

    correct_context.set_deadline(
        std::chrono::system_clock::now() +
        2s);

    ASSERT_TRUE(
        stub->CancelTask(
            &correct_context,
            correct_request,
            &correct_response).ok());

    ASSERT_TRUE(
        serialization::FromProtoStatus(
            correct_response.status()).ok());

    ASSERT_TRUE(
        WaitForLocalState(
            runtime,
            task_id,
            ExecutionState::kCancelled,
            2s));

    server->Shutdown();

    ASSERT_TRUE(
        runtime.Shutdown().ok());
}


// ------------------------------------------------------------
// 4. Full distributed path:
//
// ControlPlane
//      ↓
// NodeManager
//      ↓
// WorkerService::CancelTask
//      ↓
// Runtime::Cancel
//      ↓
// handler observes token
//      ↓
// ResultReporter
//      ↓
// ControlPlane::ReportTaskResult
//      ↓
// KCanceled + resource released
// ------------------------------------------------------------

TEST(
    Phase3RemoteCancellationTest,
    ControlPlaneCancellationReachesWorkerAndReleasesResources)
{
    // ========================================================
    // Control-plane side
    // ========================================================

    ConnectionPool connection_pool;

    RpcClient rpc_client(
        connection_pool);

    NodeRegistry node_registry;

    NodeManager node_manager(
        node_registry,
        rpc_client);

    ControlPlane control_plane(
        node_registry,
        node_manager);

    ControlServiceImpl control_service(
        control_plane);

    grpc::ServerBuilder control_builder;

    int control_port = 0;

    control_builder.AddListeningPort(
        "127.0.0.1:0",
        grpc::InsecureServerCredentials(),
        &control_port);

    control_builder.RegisterService(
        &control_service);

    std::unique_ptr<grpc::Server> control_server =
        control_builder.BuildAndStart();

    ASSERT_NE(
        control_server,
        nullptr);

    ASSERT_GT(
        control_port,
        0);

    const std::string control_endpoint =
        "127.0.0.1:" +
        std::to_string(control_port);

    // ========================================================
    // Worker side
    // ========================================================

    Runtime runtime(
        MakeRuntimeOptions());

    ASSERT_TRUE(
        runtime.Start().ok());

    HandlerRegistry handlers;

    std::atomic_bool handler_started{false};

    ASSERT_TRUE(
        handlers.Register(
            "builtin.wait_for_cancel",
            [&handler_started](
                const TaskSpec&,
                RuntimeContext& context,
                std::string_view,
                std::string_view,
                std::string*) -> Status
            {
                handler_started.store(
                    true,
                    std::memory_order_release);

                while(!context.IsCancellationRequested())
                {
                    std::this_thread::sleep_for(
                        1ms);
                }

                return Status::Cancelled(
                    "cancelled by control plane");
            }).ok());

    ResultReporter result_reporter(
        rpc_client,
        control_endpoint);

    WorkerServiceImpl worker_service(
        runtime,
        handlers,
        &result_reporter);

    grpc::ServerBuilder worker_builder;

    int worker_port = 0;

    worker_builder.AddListeningPort(
        "127.0.0.1:0",
        grpc::InsecureServerCredentials(),
        &worker_port);

    worker_builder.RegisterService(
        &worker_service);

    std::unique_ptr<grpc::Server> worker_server =
        worker_builder.BuildAndStart();

    ASSERT_NE(
        worker_server,
        nullptr);

    ASSERT_GT(
        worker_port,
        0);

    const std::string worker_endpoint =
        "127.0.0.1:" +
        std::to_string(worker_port);

    // ========================================================
    // Register worker node
    // ========================================================

    const NodeID node_id =
        NodeID::Random();

    NodeRecord node;

    node.node_id =
        node_id;

    node.worker_endpoint =
        worker_endpoint;

    node.total_resources =
        MakeResources(1.0);

    node.reported_available =
        MakeResources(1.0);

    node.incarnation = 1;

    node.state =
        NodeState::kAlive;

    ASSERT_TRUE(
        control_plane.RegisterNode(
            std::move(node)).ok());

    // ========================================================
    // Submit distributed task
    // ========================================================

    SubmitTaskSpec submission;

    submission.tenant_id =
        TenantID::Random();

    submission.agent_id =
        AgentID::Random();

    submission.name =
        "distributed remote cancellation";

    submission.entrypoint =
        "builtin.wait_for_cancel";

    submission.resources =
        ResourceRequest(
            MakeResources(1.0));

    submission.payload = "";

    submission.payload_media_type =
        "text/plain";

    SubmitTaskResult submit_result;

    ASSERT_TRUE(
        control_plane.SubmitTask(
            submission,
            &submit_result).ok());

    EXPECT_EQ(
        submit_result.node_id,
        node_id);

    // Reservation must now be consumed.
    NodeRecord after_submit;

    ASSERT_TRUE(
        node_registry.GetNode(
            node_id,
            &after_submit).ok());

    EXPECT_DOUBLE_EQ(
        after_submit
            .schedulable_available
            .Get("CPU")
            .ToDouble(),
        0.0);

    // Ensure handler is genuinely executing before cancellation.
    const auto start_deadline =
        std::chrono::steady_clock::now() +
        2s;

    while(
        !handler_started.load(
            std::memory_order_acquire) &&
        std::chrono::steady_clock::now() <
            start_deadline)
    {
        std::this_thread::sleep_for(
            1ms);
    }

    ASSERT_TRUE(
        handler_started.load(
            std::memory_order_acquire));

    // ========================================================
    // Cancel through CONTROL PLANE.
    // ========================================================

    ASSERT_TRUE(
        control_plane.CancelTask(
            submit_result.task_id,
            2s).ok());

    // Important 5G semantic:
    //
    // CancelTask() returning OK means the cancellation request
    // reached/was accepted by the worker.
    //
    // Terminal KCanceled comes from ReportTaskResult.
    ASSERT_TRUE(
        WaitForDistributedState(
            control_plane,
            submit_result.task_id,
            DistributedTaskState::KCanceled,
            2s));

    DistributedTaskRecord final_record;

    ASSERT_TRUE(
        control_plane.GetTaskRecord(
            submit_result.task_id,
            &final_record).ok());

    EXPECT_EQ(
        final_record.execution_id,
        submit_result.execution_id);

    EXPECT_EQ(
        final_record.state,
        DistributedTaskState::KCanceled);

    EXPECT_TRUE(
        final_record.terminal_status.IsCancelled());

    // ========================================================
    // Reservation must have been released exactly once by the
    // normal ReportTaskResult terminal path.
    // ========================================================

    NodeRecord after_cancel;

    ASSERT_TRUE(
        node_registry.GetNode(
            node_id,
            &after_cancel).ok());

    EXPECT_DOUBLE_EQ(
        after_cancel
            .schedulable_available
            .Get("CPU")
            .ToDouble(),
        1.0);

    // ========================================================
    // Shutdown
    // ========================================================

    worker_server->Shutdown();
    control_server->Shutdown();

    ASSERT_TRUE(
        runtime.Shutdown().ok());
}

}  // namespace
}  // namespace dar

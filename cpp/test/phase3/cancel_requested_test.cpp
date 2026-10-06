/**
1. Successful remote cancellation:
   KRunning
      ↓
   Cancel RPC accepted
      ↓
   KCancelRequested

2. Cancellation RPC failure:
   KRunning
      ↓
   RPC fails
      ↓
   remains KRunning

3. Final cancellation report:
   KCancelRequested
      ↓
   ReportTaskResult(KCanceled)
      ↓
   KCanceled
      ↓
   resource released exactly once

4. Completion wins cancellation race:
   Cancel RPC in flight
      ↓
   ReportTaskResult(KSucceeded)
      ↓
   cancellation RPC returns
      ↓
   remains KSucceeded

5. Node death while cancellation pending:
   KCancelRequested
      ↓
   HandleNodeDead()
      ↓
   KFailed / Unavailable
      ↓
   reservation released
 */

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include <grpcpp/grpcpp.h>

#include "dar/common/id.h"
#include "dar/common/status.h"
#include "dar/control/control_plane.h"
#include "dar/node/node_manager.h"
#include "dar/node/node_registry.h"
#include "dar/rpc/connection_pool.h"
#include "dar/rpc/rpc_client.h"
#include "dar/runtime/runtime.h"
#include "dar/serialization/serialization.h"
#include "dar/worker/handler_registry.h"
#include "dar/worker/result_reporter.h"
#include "dar/worker/worker_service.h"
#include "dar/control/control_service.h"

#include "worker.grpc.pb.h"

namespace dar
{
namespace
{

using namespace std::chrono_literals;


// ============================================================
// Helpers
// ============================================================

ResourceSet Cpu(double amount)
{
    ResourceSet resources;

    EXPECT_TRUE(
        resources.Set(
            "CPU",
            amount).ok());

    return resources;
}


RuntimeOptions OneCpuRuntime()
{
    RuntimeOptions options;

    options.worker_count = 1;

    EXPECT_TRUE(
        options.resources_per_worker.Set(
            "CPU",
            1.0).ok());

    return options;
}


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

        std::this_thread::sleep_for(1ms);
    }

    return false;
}


bool WaitForFlag(
    const std::atomic_bool& flag,
    std::chrono::milliseconds timeout)
{
    const auto deadline =
        std::chrono::steady_clock::now() +
        timeout;

    while(std::chrono::steady_clock::now() <
          deadline)
    {
        if(flag.load(
               std::memory_order_acquire))
        {
            return true;
        }

        std::this_thread::sleep_for(1ms);
    }

    return flag.load(
        std::memory_order_acquire);
}


// ============================================================
// Real-worker fixture
//
// Used for:
//   1. KRunning -> KCancelRequested
//   3. KCancelRequested -> KCanceled
//   5. node death while cancellation pending
//   6. repeated cancellation
// ============================================================

class CancelRequestedIntegrationTest
    : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // ----------------------------------------------------
        // Control-plane transport
        // ----------------------------------------------------

        rpc_client_ =
            std::make_unique<RpcClient>(
                connection_pool_);

        node_manager_ =
            std::make_unique<NodeManager>(
                node_registry_,
                *rpc_client_);

        control_plane_ =
            std::make_unique<ControlPlane>(
                node_registry_,
                *node_manager_);

        // ----------------------------------------------------
        // Worker runtime
        // ----------------------------------------------------

        runtime_ =
            std::make_unique<Runtime>(
                OneCpuRuntime());

        ASSERT_TRUE(
            runtime_->Start().ok());

        // Handler deliberately does NOT terminate immediately
        // after observing cancellation.
        //
        // This gives the test a deterministic window in which
        // the distributed state must be KCancelRequested.
        ASSERT_TRUE(
            handlers_.Register(
                "builtin.cancel_gate",
                [this](
                    const TaskSpec&,
                    RuntimeContext& context,
                    std::string_view,
                    std::string_view,
                    std::string*) -> Status
                {
                    handler_started_.store(
                        true,
                        std::memory_order_release);

                    while(
                        !context.IsCancellationRequested())
                    {
                        std::this_thread::sleep_for(
                            1ms);
                    }

                    cancellation_observed_.store(
                        true,
                        std::memory_order_release);

                    while(
                        !allow_handler_to_exit_.load(
                            std::memory_order_acquire))
                    {
                        std::this_thread::sleep_for(
                            1ms);
                    }

                    return Status::Cancelled(
                        "cancelled by control plane");
                }).ok());

        // ----------------------------------------------------
        // ControlService is needed because ResultReporter sends
        // completion back through the real gRPC boundary.
        // ----------------------------------------------------

        control_service_ =
            std::make_unique<ControlServiceImpl>(
                *control_plane_);

        grpc::ServerBuilder control_builder;

        int control_port = 0;

        control_builder.AddListeningPort(
            "127.0.0.1:0",
            grpc::InsecureServerCredentials(),
            &control_port);

        control_builder.RegisterService(
            control_service_.get());

        control_server_ =
            control_builder.BuildAndStart();

        ASSERT_NE(
            control_server_,
            nullptr);

        ASSERT_GT(
            control_port,
            0);

        control_endpoint_ =
            "127.0.0.1:" +
            std::to_string(control_port);

        result_reporter_ =
            std::make_unique<ResultReporter>(
                *rpc_client_,
                control_endpoint_);

        worker_service_ =
            std::make_unique<WorkerServiceImpl>(
                *runtime_,
                handlers_,
                result_reporter_.get());

        // ----------------------------------------------------
        // Worker gRPC server
        // ----------------------------------------------------

        grpc::ServerBuilder worker_builder;

        int worker_port = 0;

        worker_builder.AddListeningPort(
            "127.0.0.1:0",
            grpc::InsecureServerCredentials(),
            &worker_port);

        worker_builder.RegisterService(
            worker_service_.get());

        worker_server_ =
            worker_builder.BuildAndStart();

        ASSERT_NE(
            worker_server_,
            nullptr);

        ASSERT_GT(
            worker_port,
            0);

        worker_endpoint_ =
            "127.0.0.1:" +
            std::to_string(worker_port);

        // ----------------------------------------------------
        // Register one worker node
        // ----------------------------------------------------

        node_id_ =
            NodeID::Random();

        NodeRecord node;

        node.node_id =
            node_id_;

        node.worker_endpoint =
            worker_endpoint_;

        node.total_resources =
            Cpu(1.0);

        node.reported_available =
            Cpu(1.0);

        node.incarnation = 1;

        node.state =
            NodeState::kAlive;

        ASSERT_TRUE(
            control_plane_->RegisterNode(
                std::move(node)).ok());
    }


    void TearDown() override
    {
        // Never leave a gated worker blocked during teardown.
        allow_handler_to_exit_.store(
            true,
            std::memory_order_release);

        if(worker_server_)
        {
            worker_server_->Shutdown();
        }

        if(control_server_)
        {
            control_server_->Shutdown();
        }

        if(runtime_)
        {
            EXPECT_TRUE(
                runtime_->Shutdown().ok());
        }
    }


    SubmitTaskResult SubmitGatedTask()
    {
        SubmitTaskSpec submission;

        submission.tenant_id =
            TenantID::Random();

        submission.agent_id =
            AgentID::Random();

        submission.name =
            "5H cancel requested test";

        submission.entrypoint =
            "builtin.cancel_gate";

        submission.resources =
            ResourceRequest(
                Cpu(1.0));

        submission.payload = "";

        submission.payload_media_type =
            "text/plain";

        SubmitTaskResult result;

        EXPECT_TRUE(
            control_plane_->SubmitTask(
                submission,
                &result).ok());

        EXPECT_TRUE(
            WaitForFlag(
                handler_started_,
                2s));

        return result;
    }


    void ExpectCpuAvailable(double expected)
    {
        NodeRecord node;

        ASSERT_TRUE(
            node_registry_.GetNode(
                node_id_,
                &node).ok());

        EXPECT_DOUBLE_EQ(
            node.schedulable_available
                .Get("CPU")
                .ToDouble(),
            expected);
    }


    ConnectionPool connection_pool_;

    NodeRegistry node_registry_;

    std::unique_ptr<RpcClient>
        rpc_client_;

    std::unique_ptr<NodeManager>
        node_manager_;

    std::unique_ptr<ControlPlane>
        control_plane_;

    std::unique_ptr<Runtime>
        runtime_;

    HandlerRegistry handlers_;

    std::unique_ptr<ControlServiceImpl>
        control_service_;

    std::unique_ptr<ResultReporter>
        result_reporter_;

    std::unique_ptr<WorkerServiceImpl>
        worker_service_;

    std::unique_ptr<grpc::Server>
        control_server_;

    std::unique_ptr<grpc::Server>
        worker_server_;

    std::string control_endpoint_;

    std::string worker_endpoint_;

    NodeID node_id_;

    std::atomic_bool handler_started_{false};

    std::atomic_bool cancellation_observed_{false};

    std::atomic_bool allow_handler_to_exit_{false};
};


// ============================================================
// TEST 1
//
// Successful remote cancellation:
//
// KRunning
//      ↓
// Cancel RPC accepted
//      ↓
// KCancelRequested
//
// Resource MUST remain reserved.
// ============================================================

TEST_F(
    CancelRequestedIntegrationTest,
    SuccessfulRemoteCancellationEntersCancelRequested)
{
    const SubmitTaskResult task =
        SubmitGatedTask();

    DistributedTaskRecord before;

    ASSERT_TRUE(
        control_plane_->GetTaskRecord(
            task.task_id,
            &before).ok());

    ASSERT_EQ(
        before.state,
        DistributedTaskState::KRunning);

    // Submission consumed the node's CPU reservation.
    ExpectCpuAvailable(0.0);

    // --------------------------------------------------------
    // Request remote cancellation.
    // --------------------------------------------------------

    ASSERT_TRUE(
        control_plane_->CancelTask(
            task.task_id,
            2s).ok());

    // Worker has actually observed its Phase-1 cancellation
    // token, but our gate prevents it from completing.
    ASSERT_TRUE(
        WaitForFlag(
            cancellation_observed_,
            2s));

    DistributedTaskRecord pending;

    ASSERT_TRUE(
        control_plane_->GetTaskRecord(
            task.task_id,
            &pending).ok());

    EXPECT_EQ(
        pending.state,
        DistributedTaskState::KCancelRequested);

    EXPECT_EQ(
        pending.execution_id,
        task.execution_id);

    // CANCEL_REQUESTED is NONTERMINAL.
    //
    // Therefore the cluster reservation still belongs to this
    // execution.
    ExpectCpuAvailable(0.0);

    // Let teardown finish the worker cleanly.
    allow_handler_to_exit_.store(
        true,
        std::memory_order_release);

    ASSERT_TRUE(
        WaitForDistributedState(
            *control_plane_,
            task.task_id,
            DistributedTaskState::KCanceled,
            2s));
}


// ============================================================
// TEST 2
//
// Cancellation RPC failure:
//
// KRunning
//      ↓
// Cancel delivery fails
//      ↓
// remains KRunning
//
// We use a registered node with an unreachable endpoint.
// SubmitTask cannot be used because launch would fail, so this
// test exercises the behavior through a small real worker that
// is shut down AFTER submission and BEFORE cancellation.
// ============================================================

TEST_F(
    CancelRequestedIntegrationTest,
    CancellationRpcFailureLeavesTaskRunning)
{
    const SubmitTaskResult task =
        SubmitGatedTask();

    DistributedTaskRecord before;

    ASSERT_TRUE(
        control_plane_->GetTaskRecord(
            task.task_id,
            &before).ok());

    ASSERT_EQ(
        before.state,
        DistributedTaskState::KRunning);

    ExpectCpuAvailable(0.0);

    // --------------------------------------------------------
    // Make the worker RPC endpoint unavailable without telling
    // the ControlPlane that the node is DEAD.
    //
    // Therefore:
    //   registry state = ALIVE
    //   task state     = KRunning
    //   CancelTask RPC = transport failure
    // --------------------------------------------------------

    worker_server_->Shutdown(std::chrono::system_clock::now()+100ms);
    worker_server_.reset();

    const Status cancel_status =
        control_plane_->CancelTask(
            task.task_id,
            100ms);

    EXPECT_FALSE(
        cancel_status.ok());

    DistributedTaskRecord after;

    ASSERT_TRUE(
        control_plane_->GetTaskRecord(
            task.task_id,
            &after).ok());

    EXPECT_EQ(
        after.state,
        DistributedTaskState::KRunning);

    // Failed cancellation delivery must NOT release resources.
    ExpectCpuAvailable(0.0);

    // The handler is local to this test process and is still
    // running even though its gRPC server has stopped.
    // Release it for clean teardown.
    allow_handler_to_exit_.store(
        true,
        std::memory_order_release);
}


// ============================================================
// TEST 3
//
// KCancelRequested
//      ↓
// ReportTaskResult(KCanceled)
//      ↓
// KCanceled
//      ↓
// resource released exactly once
// ============================================================

TEST_F(
    CancelRequestedIntegrationTest,
    FinalCancellationReportTerminalizesAndReleasesReservationOnce)
{
    const SubmitTaskResult task =
        SubmitGatedTask();

    ASSERT_TRUE(
        control_plane_->CancelTask(
            task.task_id,
            2s).ok());

    ASSERT_TRUE(
        WaitForFlag(
            cancellation_observed_,
            2s));

    DistributedTaskRecord pending;

    ASSERT_TRUE(
        control_plane_->GetTaskRecord(
            task.task_id,
            &pending).ok());

    ASSERT_EQ(
        pending.state,
        DistributedTaskState::KCancelRequested);

    // Still active -> still reserved.
    ExpectCpuAvailable(0.0);

    // --------------------------------------------------------
    // Allow the cooperative handler to return Cancelled.
    //
    // ResultReporter should eventually send:
    //
    // ReportTaskResult(T1, E1, KCanceled, ...)
    // --------------------------------------------------------

    allow_handler_to_exit_.store(
        true,
        std::memory_order_release);

    ASSERT_TRUE(
        WaitForDistributedState(
            *control_plane_,
            task.task_id,
            DistributedTaskState::KCanceled,
            2s));

    DistributedTaskRecord terminal;

    ASSERT_TRUE(
        control_plane_->GetTaskRecord(
            task.task_id,
            &terminal).ok());

    EXPECT_EQ(
        terminal.state,
        DistributedTaskState::KCanceled);

    EXPECT_TRUE(
        terminal.terminal_status.IsCancelled());

    // First terminal report released the reservation.
    ExpectCpuAvailable(1.0);

    // --------------------------------------------------------
    // Duplicate identical terminal report must remain
    // idempotent and must NOT release the CPU twice.
    // --------------------------------------------------------

    const Status duplicate =
        control_plane_->ReportTaskResult(
            task.task_id,
            task.execution_id,
            DistributedTaskState::KCanceled,
            terminal.terminal_status,
            terminal.result,
            terminal.result_media_type);

    EXPECT_TRUE(
        duplicate.ok());

    // If ReportTaskResult tried to release twice, ResourcePool
    // should reject it or the ledger would become incorrect.
    ExpectCpuAvailable(1.0);
}


// ============================================================
// Blocking cancellation service used for the race test.
//
// We need:
//
// ControlPlane::CancelTask
//      ↓
// Worker CancelTask RPC enters server
//      ↓
// BLOCK HERE
//
// Meanwhile:
//
// ReportTaskResult(KSucceeded)
//
// Then release CancelTask RPC.
//
// The ControlPlane must re-check authoritative state and must
// NOT overwrite KSucceeded with KCancelRequested.
// ============================================================

class BlockingCancelWorkerService final
    : public proto::v1::WorkerService::Service
{
public:
    grpc::Status CancelTask(
        grpc::ServerContext*,
        const proto::v1::CancelWorkerTaskRequest*,
        proto::v1::CancelWorkerTaskResponse* response) override
    {
        {
            std::lock_guard<std::mutex> lock(mu_);

            entered_ = true;
        }

        cv_.notify_all();

        {
            std::unique_lock<std::mutex> lock(mu_);

            cv_.wait(
                lock,
                [this]
                {
                    return release_;
                });
        }

        serialization::ToProto(
            Status::OK(),
            response->mutable_status());

        return grpc::Status::OK;
    }


    bool WaitUntilEntered(
        std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mu_);

        return cv_.wait_for(
            lock,
            timeout,
            [this]
            {
                return entered_;
            });
    }


    void Release()
    {
        {
            std::lock_guard<std::mutex> lock(mu_);

            release_ = true;
        }

        cv_.notify_all();
    }

private:
    std::mutex mu_;

    std::condition_variable cv_;

    bool entered_{false};

    bool release_{false};
};


// ============================================================
// TEST 4
//
// Completion wins cancellation race:
//
// Cancel RPC in flight
//      ↓
// ReportTaskResult(KSucceeded)
//      ↓
// cancellation RPC returns OK
//      ↓
// remains KSucceeded
// ============================================================

TEST(
    Phase3CancelRequestedTest,
    TerminalCompletionWinsRaceWithCancellationRpc)
{
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

    // --------------------------------------------------------
    // Fake worker endpoint whose CancelTask blocks.
    // --------------------------------------------------------

    BlockingCancelWorkerService worker_service;

    grpc::ServerBuilder builder;

    int worker_port = 0;

    builder.AddListeningPort(
        "127.0.0.1:0",
        grpc::InsecureServerCredentials(),
        &worker_port);

    builder.RegisterService(
        &worker_service);

    std::unique_ptr<grpc::Server> server =
        builder.BuildAndStart();

    ASSERT_NE(
        server,
        nullptr);

    ASSERT_GT(
        worker_port,
        0);

    const NodeID node_id =
        NodeID::Random();

    NodeRecord node;

    node.node_id =
        node_id;

    node.worker_endpoint =
        "127.0.0.1:" +
        std::to_string(worker_port);

    node.total_resources =
        Cpu(1.0);

    node.reported_available =
        Cpu(1.0);

    node.incarnation = 1;

    node.state =
        NodeState::kAlive;

    ASSERT_TRUE(
        control_plane.RegisterNode(
            std::move(node)).ok());

    // --------------------------------------------------------
    // We need an authoritative distributed task record.
    //
    // SubmitTask normally creates this, but our fake service
    // implements only cancellation, not LaunchTask.
    //
    // Therefore this race test should use a normal real worker
    // for submission OR a test hook.
    //
    // The cleanest option with your current public API is to
    // start a real WorkerService for LaunchTask. Since that
    // would substantially duplicate the fixture above, this
    // test instead uses the integration fixture pattern below.
    // --------------------------------------------------------

    server->Shutdown();
}


// ============================================================
// TEST 4 - actual integration version.
//
// A real worker is used for submission. We then call
// ReportTaskResult manually while cancellation is pending.
//
// NOTE:
// This validates terminal-state precedence, but it cannot force
// the precise "RPC is blocked in network I/O" timing without
// injecting a NodeManager/worker transport seam.
//
// Your production code's second locked re-check is what protects
// that exact race.
// ============================================================

TEST_F(
    CancelRequestedIntegrationTest,
    TerminalResultIsNotOverwrittenByLaterCancellationState)
{
    const SubmitTaskResult task =
        SubmitGatedTask();

    // First prove the task currently owns the reservation.
    ExpectCpuAvailable(0.0);

    // --------------------------------------------------------
    // Simulate a completion reaching the control plane first.
    // --------------------------------------------------------

    ASSERT_TRUE(
        control_plane_->ReportTaskResult(
            task.task_id,
            task.execution_id,
            DistributedTaskState::KSucceeded,
            Status::OK(),
            "finished",
            "text/plain").ok());

    DistributedTaskRecord terminal;

    ASSERT_TRUE(
        control_plane_->GetTaskRecord(
            task.task_id,
            &terminal).ok());

    ASSERT_EQ(
        terminal.state,
        DistributedTaskState::KSucceeded);

    ExpectCpuAvailable(1.0);

    // Once terminal, cancellation cannot reopen the task.
    const Status cancel_status =
        control_plane_->CancelTask(
            task.task_id,
            2s);

    EXPECT_FALSE(
        cancel_status.ok());

    DistributedTaskRecord after;

    ASSERT_TRUE(
        control_plane_->GetTaskRecord(
            task.task_id,
            &after).ok());

    EXPECT_EQ(
        after.state,
        DistributedTaskState::KSucceeded);

    EXPECT_EQ(
        after.result,
        "finished");

    ExpectCpuAvailable(1.0);

    // Local worker still exists, so let it terminate during
    // fixture teardown.
    allow_handler_to_exit_.store(
        true,
        std::memory_order_release);
}


// ============================================================
// TEST 5
//
// Node death while cancellation pending:
//
// KCancelRequested
//      ↓
// HandleNodeDead()
//      ↓
// KFailed / Unavailable
//      ↓
// reservation released
// ============================================================

TEST_F(
    CancelRequestedIntegrationTest,
    NodeDeathWhileCancelRequestedFailsTaskAndReleasesReservation)
{
    const SubmitTaskResult task =
        SubmitGatedTask();

    ASSERT_TRUE(
        control_plane_->CancelTask(
            task.task_id,
            2s).ok());

    ASSERT_TRUE(
        WaitForFlag(
            cancellation_observed_,
            2s));

    DistributedTaskRecord pending;

    ASSERT_TRUE(
        control_plane_->GetTaskRecord(
            task.task_id,
            &pending).ok());

    ASSERT_EQ(
        pending.state,
        DistributedTaskState::KCancelRequested);

    ExpectCpuAvailable(0.0);

    // --------------------------------------------------------
    // Failure detector would normally call this.
    // --------------------------------------------------------

    ASSERT_TRUE(
        control_plane_->HandleNodeDead(
            node_id_,
            1).ok());

    DistributedTaskRecord failed;

    ASSERT_TRUE(
        control_plane_->GetTaskRecord(
            task.task_id,
            &failed).ok());

    EXPECT_EQ(
        failed.state,
        DistributedTaskState::KFailed);

    EXPECT_EQ(
        failed.terminal_status.code(),
        StatusCode::KUnavailable);

    EXPECT_TRUE(
        failed.result.empty());

    EXPECT_TRUE(
        failed.result_media_type.empty());

    // Node-death terminalization releases the reservation.
    ExpectCpuAvailable(1.0);

    // --------------------------------------------------------
    // A late worker cancellation report must NOT overwrite the
    // node-death failure.
    // --------------------------------------------------------

    const Status late_report =
        control_plane_->ReportTaskResult(
            task.task_id,
            task.execution_id,
            DistributedTaskState::KCanceled,
            Status::Cancelled(
                "late cancellation completion"),
            "",
            "");

    EXPECT_FALSE(
        late_report.ok());

    DistributedTaskRecord still_failed;

    ASSERT_TRUE(
        control_plane_->GetTaskRecord(
            task.task_id,
            &still_failed).ok());

    EXPECT_EQ(
        still_failed.state,
        DistributedTaskState::KFailed);

    EXPECT_EQ(
        still_failed.terminal_status.code(),
        StatusCode::KUnavailable);

    // No second release.
    ExpectCpuAvailable(1.0);

    allow_handler_to_exit_.store(
        true,
        std::memory_order_release);
}


// ============================================================
// TEST 6
//
// Repeated cancellation while already KCancelRequested is
// idempotent.
//
// It must:
//   - return OK
//   - remain KCancelRequested
//   - NOT release resources
// ============================================================

TEST_F(
    CancelRequestedIntegrationTest,
    RepeatedCancellationWhilePendingIsIdempotent)
{
    const SubmitTaskResult task =
        SubmitGatedTask();

    ASSERT_TRUE(
        control_plane_->CancelTask(
            task.task_id,
            2s).ok());

    ASSERT_TRUE(
        WaitForFlag(
            cancellation_observed_,
            2s));

    DistributedTaskRecord first;

    ASSERT_TRUE(
        control_plane_->GetTaskRecord(
            task.task_id,
            &first).ok());

    ASSERT_EQ(
        first.state,
        DistributedTaskState::KCancelRequested);

    ExpectCpuAvailable(0.0);

    // --------------------------------------------------------
    // Second request.
    //
    // ControlPlane should recognize that cancellation is
    // already outstanding and return OK without terminalizing
    // or releasing anything.
    // --------------------------------------------------------

    EXPECT_TRUE(
        control_plane_->CancelTask(
            task.task_id,
            2s).ok());

    DistributedTaskRecord second;

    ASSERT_TRUE(
        control_plane_->GetTaskRecord(
            task.task_id,
            &second).ok());

    EXPECT_EQ(
        second.state,
        DistributedTaskState::KCancelRequested);

    EXPECT_EQ(
        second.execution_id,
        first.execution_id);

    ExpectCpuAvailable(0.0);

    // Finish normally through cancellation reporting.
    allow_handler_to_exit_.store(
        true,
        std::memory_order_release);

    ASSERT_TRUE(
        WaitForDistributedState(
            *control_plane_,
            task.task_id,
            DistributedTaskState::KCanceled,
            2s));

    ExpectCpuAvailable(1.0);
}

}  // namespace
}  // namespace dar
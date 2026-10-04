#include <chrono>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "dar/control/control_plane.h"
#include "dar/control/control_service.h"
#include "dar/node/node_manager.h"
#include "dar/node/node_registry.h"
#include "dar/rpc/connection_pool.h"
#include "dar/rpc/rpc_client.h"
#include "dar/rpc/rpc_server.h"
#include "dar/runtime/runtime.h"
#include "dar/worker/builtin_handlers.h"
#include "dar/worker/handler_registry.h"
#include "dar/worker/result_reporter.h"
#include "dar/worker/worker_service.h"

namespace dar
{
namespace
{

TEST(
    Phase3ClusterSchedulerTest,
    FirstFitExecutesAndReportsResultsToControlPlane)
{
    // ========================================================
    // Control Plane
    //
    // The control plane now has its own gRPC server because
    // workers must call ReportTaskResult() back to it.
    // ========================================================

    ConnectionPool connections;
    RpcClient rpc_client(connections);

    NodeRegistry node_registry;

    NodeManager node_manager(
        node_registry,
        rpc_client);

    ControlPlane control_plane(
        node_registry,
        node_manager);

    ControlServiceImpl control_service(
        control_plane);

    RpcServer control_server(
        "127.0.0.1:0");

    ASSERT_TRUE(
        control_server.RegisterService(
            &control_service).ok());

    ASSERT_TRUE(
        control_server.Start().ok());

    const std::string control_endpoint =
        "127.0.0.1:" +
        std::to_string(
            control_server.SelectedPort());


    // ========================================================
    // Worker Node B
    //
    // Node B now has a ResultReporter.
    //
    // Execution path:
    //
    // Runtime completes task
    //      ↓
    // completion callback
    //      ↓
    // ResultReporter
    //      ↓
    // ControlService::ReportTaskResult()
    // ========================================================

    RuntimeOptions options_b;
    options_b.worker_count = 1;

    ASSERT_TRUE(
        options_b.resources_per_worker.Set(
            "CPU",
            1.0).ok());

    Runtime runtime_b(
        std::move(options_b));

    ASSERT_TRUE(
        runtime_b.Start().ok());

    HandlerRegistry registry_b;

    ASSERT_TRUE(
        RegisterBuiltinHandlers(
            registry_b).ok());

    // Worker B needs its own client-side connection machinery
    // for calling the ControlPlane back.
    ConnectionPool worker_connections_b;

    RpcClient worker_rpc_b(
        worker_connections_b);

    ResultReporter reporter_b(
        worker_rpc_b,
        control_endpoint);

    WorkerServiceImpl worker_service_b(
        runtime_b,
        registry_b,
        &reporter_b);

    RpcServer server_b(
        "127.0.0.1:0");

    ASSERT_TRUE(
        server_b.RegisterService(
            &worker_service_b).ok());

    ASSERT_TRUE(
        server_b.Start().ok());

    const std::string endpoint_b =
        "127.0.0.1:" +
        std::to_string(
            server_b.SelectedPort());


    // ========================================================
    // Worker Node C
    // ========================================================

    RuntimeOptions options_c;
    options_c.worker_count = 1;

    ASSERT_TRUE(
        options_c.resources_per_worker.Set(
            "CPU",
            1.0).ok());

    Runtime runtime_c(
        std::move(options_c));

    ASSERT_TRUE(
        runtime_c.Start().ok());

    HandlerRegistry registry_c;

    ASSERT_TRUE(
        RegisterBuiltinHandlers(
            registry_c).ok());

    ConnectionPool worker_connections_c;

    RpcClient worker_rpc_c(
        worker_connections_c);

    ResultReporter reporter_c(
        worker_rpc_c,
        control_endpoint);

    WorkerServiceImpl worker_service_c(
        runtime_c,
        registry_c,
        &reporter_c);

    RpcServer server_c(
        "127.0.0.1:0");

    ASSERT_TRUE(
        server_c.RegisterService(
            &worker_service_c).ok());

    ASSERT_TRUE(
        server_c.Start().ok());

    const std::string endpoint_c =
        "127.0.0.1:" +
        std::to_string(
            server_c.SelectedPort());


    // ========================================================
    // Manually register Node B
    // ========================================================

    ResourceSet capacity_b;

    ASSERT_TRUE(
        capacity_b.Set(
            "CPU",
            1.0).ok());

    NodeRecord node_b;

    node_b.node_id =
        NodeID::Random();

    node_b.worker_endpoint =
        endpoint_b;

    node_b.total_resources =
        capacity_b;

    node_b.reported_available =
        capacity_b;

    node_b.incarnation = 1;

    node_b.state =
        NodeState::kAlive;

    const NodeID node_b_id =
        node_b.node_id;

    ASSERT_TRUE(
        node_manager.RegisterNode(
            std::move(node_b)).ok());


    // ========================================================
    // Manually register Node C
    // ========================================================

    ResourceSet capacity_c;

    ASSERT_TRUE(
        capacity_c.Set(
            "CPU",
            1.0).ok());

    NodeRecord node_c;

    node_c.node_id =
        NodeID::Random();

    node_c.worker_endpoint =
        endpoint_c;

    node_c.total_resources =
        capacity_c;

    node_c.reported_available =
        capacity_c;

    node_c.incarnation = 1;

    node_c.state =
        NodeState::kAlive;

    const NodeID node_c_id =
        node_c.node_id;

    ASSERT_TRUE(
        node_manager.RegisterNode(
            std::move(node_c)).ok());


    // ========================================================
    // Task #1
    //
    // First-fit should choose B.
    // ========================================================

    ResourceSet task_resources_1;

    ASSERT_TRUE(
        task_resources_1.Set(
            "CPU",
            1.0).ok());

    SubmitTaskSpec task_1;

    task_1.tenant_id =
        TenantID::Random();

    task_1.agent_id =
        AgentID::Random();

    task_1.name =
        "square-12";

    task_1.entrypoint =
        "builtin.square";

    task_1.resources =
        ResourceRequest(
            std::move(task_resources_1));

    task_1.payload =
        "12";

    task_1.payload_media_type =
        "text/plain";

    SubmitTaskResult result_1;

    ASSERT_TRUE(
        control_plane.SubmitTask(
            task_1,
            &result_1).ok());

    EXPECT_EQ(
        result_1.node_id,
        node_b_id);


    // ========================================================
    // IMPORTANT:
    //
    // Submit Task #2 immediately, before waiting for Task #1's
    // ReportTaskResult.
    //
    // This preserves the Stage-4 scheduling test:
    //
    // reported_available(B)   = 1
    // schedulable_available(B)= 0
    //
    // Therefore Task #2 must choose C.
    //
    // If we waited for Task #1 to report completion first,
    // B's reservation could already be released and first-fit
    // could legitimately choose B again.
    // ========================================================

    ResourceSet task_resources_2;

    ASSERT_TRUE(
        task_resources_2.Set(
            "CPU",
            1.0).ok());

    SubmitTaskSpec task_2;

    task_2.tenant_id =
        TenantID::Random();

    task_2.agent_id =
        AgentID::Random();

    task_2.name =
        "square-13";

    task_2.entrypoint =
        "builtin.square";

    task_2.resources =
        ResourceRequest(
            std::move(task_resources_2));

    task_2.payload =
        "13";

    task_2.payload_media_type =
        "text/plain";

    SubmitTaskResult result_2;

    ASSERT_TRUE(
        control_plane.SubmitTask(
            task_2,
            &result_2).ok());

    EXPECT_EQ(
        result_2.node_id,
        node_c_id);


    // ========================================================
    // Wait for Task #1 to reach the CONTROL PLANE.
    //
    // We deliberately do NOT use:
    //
    //     runtime_b.GetResult(...)
    //
    // as the main distributed completion assertion anymore.
    //
    // Success now means the complete loop worked:
    //
    // Worker execution
    //      ↓
    // TaskManager::Complete()
    //      ↓
    // completion callback
    //      ↓
    // ResultReporter
    //      ↓
    // ReportTaskResult RPC
    //      ↓
    // ControlPlane
    // ========================================================

    DistributedTaskRecord observed_task_1;

    bool task_1_completed = false;

    for(int i = 0; i < 200; ++i)
    {
        ASSERT_TRUE(
            control_plane.GetTaskRecord(
                result_1.task_id,
                &observed_task_1).ok());

        if(
            observed_task_1.state ==
            DistributedTaskState::KSucceeded)
        {
            task_1_completed = true;
            break;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(10));
    }

    ASSERT_TRUE(task_1_completed);

    EXPECT_EQ(
        observed_task_1.task_id,
        result_1.task_id);

    // Proves that the distributed ExecutionID created by the
    // ControlPlane survived the remote execution/report path.
    EXPECT_EQ(
        observed_task_1.execution_id,
        result_1.execution_id);

    EXPECT_EQ(
        observed_task_1.node_id,
        node_b_id);

    EXPECT_TRUE(
        observed_task_1.terminal_status.ok());

    EXPECT_EQ(
        observed_task_1.result,
        "144");

    EXPECT_EQ(
        observed_task_1.result_media_type,
        "text/plain");


    // ========================================================
    // Wait for Task #2 to reach the CONTROL PLANE
    // ========================================================

    DistributedTaskRecord observed_task_2;

    bool task_2_completed = false;

    for(int i = 0; i < 200; ++i)
    {
        ASSERT_TRUE(
            control_plane.GetTaskRecord(
                result_2.task_id,
                &observed_task_2).ok());

        if(
            observed_task_2.state ==
            DistributedTaskState::KSucceeded)
        {
            task_2_completed = true;
            break;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(10));
    }

    ASSERT_TRUE(task_2_completed);

    EXPECT_EQ(
        observed_task_2.task_id,
        result_2.task_id);

    EXPECT_EQ(
        observed_task_2.execution_id,
        result_2.execution_id);

    EXPECT_EQ(
        observed_task_2.node_id,
        node_c_id);

    EXPECT_TRUE(
        observed_task_2.terminal_status.ok());

    EXPECT_EQ(
        observed_task_2.result,
        "169");

    EXPECT_EQ(
        observed_task_2.result_media_type,
        "text/plain");


    // ========================================================
    // Verify cluster resource reservations were released.
    //
    // Both tasks have now reported terminal completion.
    //
    // Therefore:
    //
    // reported_available    = 1
    // schedulable_available = 1
    //
    // reported_available remains the worker's last reported
    // capacity.
    //
    // schedulable_available was:
    //
    //     1 -> 0 on SubmitTask
    //     0 -> 1 on ReportTaskResult
    // ========================================================

    NodeRecord observed_b;

    ASSERT_TRUE(
        node_registry.GetNode(
            node_b_id,
            &observed_b).ok());

    EXPECT_DOUBLE_EQ(
        observed_b.reported_available
            .Get("CPU")
            .ToDouble(),
        1.0);

    EXPECT_DOUBLE_EQ(
        observed_b.schedulable_available
            .Get("CPU")
            .ToDouble(),
        1.0);


    NodeRecord observed_c;

    ASSERT_TRUE(
        node_registry.GetNode(
            node_c_id,
            &observed_c).ok());

    EXPECT_DOUBLE_EQ(
        observed_c.reported_available
            .Get("CPU")
            .ToDouble(),
        1.0);

    EXPECT_DOUBLE_EQ(
        observed_c.schedulable_available
            .Get("CPU")
            .ToDouble(),
        1.0);


    // ========================================================
    // Shutdown
    //
    // Stop worker RPC servers first so they cannot accept new
    // LaunchTask calls.
    //
    // Then drain/shutdown local runtimes.
    //
    // Finally stop the control server after workers no longer
    // need to send ReportTaskResult.
    // ========================================================

    ASSERT_TRUE(
        server_b.Shutdown().ok());

    ASSERT_TRUE(
        server_c.Shutdown().ok());

    ASSERT_TRUE(
        runtime_b.Shutdown().ok());

    ASSERT_TRUE(
        runtime_c.Shutdown().ok());

    ASSERT_TRUE(
        control_server.Shutdown().ok());
}

TEST(
    Phase3ClusterSchedulerTest,
    DuplicateReportTaskResultIsIdempotent)
{
    // --------------------------------------------------------
    // Control-plane infrastructure
    // --------------------------------------------------------

    ConnectionPool connections;
    RpcClient rpc_client(connections);

    NodeRegistry node_registry;

    NodeManager node_manager(
        node_registry,
        rpc_client);

    ControlPlane control_plane(
        node_registry,
        node_manager);

    // --------------------------------------------------------
    // Worker
    // --------------------------------------------------------

    RuntimeOptions options;
    options.worker_count = 1;

    ASSERT_TRUE(
        options.resources_per_worker.Set(
            "CPU",
            1.0).ok());

    Runtime runtime(std::move(options));

    ASSERT_TRUE(
        runtime.Start().ok());

    HandlerRegistry registry;

    ASSERT_TRUE(
        RegisterBuiltinHandlers(
            registry).ok());

    WorkerServiceImpl worker_service(
        runtime,
        registry);

    RpcServer worker_server(
        "127.0.0.1:0");

    ASSERT_TRUE(
        worker_server.RegisterService(
            &worker_service).ok());

    ASSERT_TRUE(
        worker_server.Start().ok());

    const std::string worker_endpoint =
        "127.0.0.1:" +
        std::to_string(
            worker_server.SelectedPort());

    // --------------------------------------------------------
    // Register one worker node
    // --------------------------------------------------------

    ResourceSet capacity;

    ASSERT_TRUE(
        capacity.Set(
            "CPU",
            1.0).ok());

    NodeRecord node;

    node.node_id =
        NodeID::Random();

    node.worker_endpoint =
        worker_endpoint;

    node.total_resources =
        capacity;

    node.reported_available =
        capacity;

    node.incarnation = 1;

    node.state =
        NodeState::kAlive;

    const NodeID node_id =
        node.node_id;

    ASSERT_TRUE(
        node_manager.RegisterNode(
            std::move(node)).ok());

    // --------------------------------------------------------
    // Submit T1 / E1
    // --------------------------------------------------------

    ResourceSet task_resources;

    ASSERT_TRUE(
        task_resources.Set(
            "CPU",
            1.0).ok());

    SubmitTaskSpec submission;

    submission.tenant_id =
        TenantID::Random();

    submission.agent_id =
        AgentID::Random();

    submission.name =
        "square-12";

    submission.entrypoint =
        "builtin.square";

    submission.resources =
        ResourceRequest(
            std::move(task_resources));

    submission.payload =
        "12";

    submission.payload_media_type =
        "text/plain";

    SubmitTaskResult submitted;

    ASSERT_TRUE(
        control_plane.SubmitTask(
            submission,
            &submitted).ok());

    EXPECT_EQ(
        submitted.node_id,
        node_id);
    
    // Verify the node returned by SubmitTask is still registered.
NodeRecord registered_after_submit;

ASSERT_TRUE(
    node_registry.GetNode(
        submitted.node_id,
        &registered_after_submit).ok());

EXPECT_EQ(
    registered_after_submit.node_id,
    submitted.node_id);

// Verify the ControlPlane stored the same NodeID.
DistributedTaskRecord record_after_submit;

ASSERT_TRUE(
    control_plane.GetTaskRecord(
        submitted.task_id,
        &record_after_submit).ok());

EXPECT_EQ(
    record_after_submit.node_id,
    submitted.node_id);

EXPECT_EQ(
    record_after_submit.node_id,
    node_id);  

    // --------------------------------------------------------
    // Wait for local execution.
    //
    // This worker intentionally has NO ResultReporter.
    // We will simulate ReportTaskResult manually so we can
    // deliver exactly the same completion twice.
    // --------------------------------------------------------

    std::string local_result;

    ASSERT_TRUE(
        runtime.GetResult(
            submitted.task_id,
            &local_result,
            std::chrono::seconds(2)).ok());

    ASSERT_EQ(
        local_result,
        "144");

    // Reservation should still be held because nothing has
    // reported terminal completion to the ControlPlane yet.

    NodeRecord before_report;

    ASSERT_TRUE(
        node_registry.GetNode(
            node_id,
            &before_report).ok());

    EXPECT_DOUBLE_EQ(
        before_report.schedulable_available
            .Get("CPU")
            .ToDouble(),
        0.0);

    // --------------------------------------------------------
    // FIRST ReportTaskResult(E1)
    // --------------------------------------------------------

    const Status first_report =
        control_plane.ReportTaskResult(
            submitted.task_id,
            submitted.execution_id,
            DistributedTaskState::KSucceeded,
            Status::OK(),
            "144",
            "text/plain");

    ASSERT_TRUE(
        first_report.ok())
        << first_report.ToString();

    NodeRecord after_first_report;

    ASSERT_TRUE(
        node_registry.GetNode(
            node_id,
            &after_first_report).ok());

    // Reservation released exactly once.
    EXPECT_DOUBLE_EQ(
        after_first_report.schedulable_available
            .Get("CPU")
            .ToDouble(),
        1.0);

    
    
    // --------------------------------------------------------
    // DUPLICATE ReportTaskResult(E1)
    // --------------------------------------------------------

    const Status duplicate_report =
        control_plane.ReportTaskResult(
            submitted.task_id,
            submitted.execution_id,
            DistributedTaskState::KSucceeded,
            Status::OK(),
            "144",
            "text/plain");

    
    // Identical duplicate is an idempotent success.
    ASSERT_TRUE(
        duplicate_report.ok())
        << duplicate_report.ToString();

    NodeRecord after_duplicate_report;

    ASSERT_TRUE(
        node_registry.GetNode(
            node_id,
            &after_duplicate_report).ok());

    // CRITICAL 5C INVARIANT:
    //
    // The duplicate did NOT release CPU again.
    EXPECT_DOUBLE_EQ(
        after_duplicate_report.schedulable_available
            .Get("CPU")
            .ToDouble(),
        1.0);

    // After the successful first report:

/**
 *                     ReportTaskResult

First E1 completion
       │
       └── commit + Release                    ✓

Same E1 + same completion
       │
       └── return OK, no Release               ✓

Same E1 + conflicting completion
       │
       └── FailedPrecondition, no Release      ✓
 */
const Status conflicting_report =
    control_plane.ReportTaskResult(
        submitted.task_id,
        submitted.execution_id,
        DistributedTaskState::KFailed,
        Status::Internal(
            "conflicting failure"),
        "",
        "text/plain");

EXPECT_FALSE(
    conflicting_report.ok());

NodeRecord after_conflict;

ASSERT_TRUE(
    node_registry.GetNode(
        node_id,
        &after_conflict).ok());

EXPECT_DOUBLE_EQ(
    after_conflict.schedulable_available
        .Get("CPU")
        .ToDouble(),
    1.0);


    // --------------------------------------------------------
    // Stored terminal result remains unchanged
    // --------------------------------------------------------

    DistributedTaskRecord record;

    ASSERT_TRUE(
        control_plane.GetTaskRecord(
            submitted.task_id,
            &record).ok());

    EXPECT_EQ(
        record.execution_id,
        submitted.execution_id);

    EXPECT_EQ(
        record.state,
        DistributedTaskState::KSucceeded);

    EXPECT_TRUE(
        record.terminal_status.ok());

    EXPECT_EQ(
        record.result,
        "144");

    EXPECT_EQ(
        record.result_media_type,
        "text/plain");

    ASSERT_TRUE(
        worker_server.Shutdown().ok());

    ASSERT_TRUE(
        runtime.Shutdown().ok());
}

// Idempotency doesn't mean accept anything after completion


}  // namespace
}  // namespace dar
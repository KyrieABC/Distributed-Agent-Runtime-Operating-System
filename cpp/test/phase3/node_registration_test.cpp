#include <chrono>
#include <string>

#include <gtest/gtest.h>

#include "dar/control/control_plane.h"
#include "dar/control/control_service.h"
#include "dar/node/node_manager.h"
#include "dar/node/node_registry.h"
#include "dar/rpc/connection_pool.h"
#include "dar/rpc/rpc_client.h"
#include "dar/rpc/rpc_server.h"
#include "dar/serialization/serialization.h"

#include "control.grpc.pb.h"


/**
                   Control Plane
                        │
              ControlService RPC
                        │
             ┌──────────┴──────────┐
             │                     │
       RegisterNode           RemoveNode
             │                     │
             ▼                     ▼
         NodeManager           ControlPlane
             │                     │
             └──────────┬──────────┘
                        ▼
                   NodeRegistry
                        │
             ┌──────────┼──────────┐
             ▼          ▼          ▼
           N1/1       N1/2       stale N1/1
          initial     restart       rejected
 */
namespace dar
{
namespace
{

void FillNodeDescriptor(
    proto::v1::NodeDescriptor* node,
    NodeID node_id,
    std::uint64_t incarnation,
    const std::string& endpoint)
{
    ASSERT_NE(
        node,
        nullptr);

    serialization::ToProto(
        node_id,
        node->mutable_node_id());

    node->set_worker_endpoint(
        endpoint);

    node->set_incarnation(
        incarnation);

    node->set_state(
        proto::v1::NODE_STATE_ALIVE);

    (*node->mutable_total_resources()
          ->mutable_quantities())["CPU"] =
        1.0;

    (*node->mutable_reported_available_resources()
          ->mutable_quantities())["CPU"] =
        1.0;
}


TEST(
    Phase3NodeRegistrationTest,
    RegistrationIsIncarnationAwareAcrossRpcBoundary)
{
    // --------------------------------------------------------
    // Control plane
    // --------------------------------------------------------

    ConnectionPool connections;

    RpcClient rpc_client(
        connections);

    NodeRegistry node_registry;

    NodeManager node_manager(
        node_registry,
        rpc_client);

    ControlPlane control_plane(
        node_registry,
        node_manager);

    ControlServiceImpl control_service(
        control_plane);

    RpcServer server(
        "127.0.0.1:0");

    ASSERT_TRUE(
        server.RegisterService(
            &control_service).ok());

    ASSERT_TRUE(
        server.Start().ok());

    const std::string endpoint =
        "127.0.0.1:" +
        std::to_string(
            server.SelectedPort());

    // --------------------------------------------------------
    // Worker-side ControlService client
    // --------------------------------------------------------

    ConnectionPool worker_connections;

    RpcClient worker_client(
        worker_connections);

    auto channel =
        worker_client.Channel(
            endpoint);

    auto stub =
        proto::v1::ControlService::NewStub(
            channel);

    const NodeID node_id =
        NodeID::Random();

    // ========================================================
    // Register N1 / incarnation 1
    // ========================================================

    proto::v1::RegisterNodeRequest first_request;

    FillNodeDescriptor(
        first_request.mutable_node(),
        node_id,
        1,
        "127.0.0.1:50051");

    proto::v1::RegisterNodeResponse first_response;

    auto first_context =
        worker_client.CreateContext(
            std::chrono::seconds(2));

    const grpc::Status first_transport =
        stub->RegisterNode(
            first_context.get(),
            first_request,
            &first_response);

    ASSERT_TRUE(
        first_transport.ok())
        << first_transport.error_message();

    Status first_status =
        serialization::FromProtoStatus(
            first_response.status());

    ASSERT_TRUE(
        first_status.ok())
        << first_status.ToString();

    NodeRecord stored;

    ASSERT_TRUE(
        node_registry.GetNode(
            node_id,
            &stored).ok());

    EXPECT_EQ(
        stored.incarnation,
        1U);

    // ========================================================
    // Retry exact same registration.
    //
    // Must be idempotent.
    // ========================================================

    proto::v1::RegisterNodeResponse duplicate_response;

    auto duplicate_context =
        worker_client.CreateContext(
            std::chrono::seconds(2));

    const grpc::Status duplicate_transport =
        stub->RegisterNode(
            duplicate_context.get(),
            first_request,
            &duplicate_response);

    ASSERT_TRUE(
        duplicate_transport.ok());

    const Status duplicate_status =
        serialization::FromProtoStatus(
            duplicate_response.status());

    EXPECT_TRUE(
        duplicate_status.ok())
        << duplicate_status.ToString();

    // ========================================================
    // Restart:
    //
    // N1 / incarnation 2
    // ========================================================

    proto::v1::RegisterNodeRequest restart_request;

    FillNodeDescriptor(
        restart_request.mutable_node(),
        node_id,
        2,
        "127.0.0.1:50052");

    proto::v1::RegisterNodeResponse restart_response;

    auto restart_context =
        worker_client.CreateContext(
            std::chrono::seconds(2));

    const grpc::Status restart_transport =
        stub->RegisterNode(
            restart_context.get(),
            restart_request,
            &restart_response);

    ASSERT_TRUE(
        restart_transport.ok());

    const Status restart_status =
        serialization::FromProtoStatus(
            restart_response.status());

    ASSERT_TRUE(
        restart_status.ok())
        << restart_status.ToString();

    ASSERT_TRUE(
        node_registry.GetNode(
            node_id,
            &stored).ok());

    EXPECT_EQ(
        stored.incarnation,
        2U);

    EXPECT_EQ(
        stored.worker_endpoint,
        "127.0.0.1:50052");

    // ========================================================
    // Delayed old registration:
    //
    // N1 / incarnation 1
    //
    // Must NOT overwrite incarnation 2.
    // ========================================================

    proto::v1::RegisterNodeResponse stale_response;

    auto stale_context =
        worker_client.CreateContext(
            std::chrono::seconds(2));

    const grpc::Status stale_transport =
        stub->RegisterNode(
            stale_context.get(),
            first_request,
            &stale_response);

    ASSERT_TRUE(
        stale_transport.ok());

    const Status stale_status =
        serialization::FromProtoStatus(
            stale_response.status());

    EXPECT_FALSE(
        stale_status.ok());

    EXPECT_EQ(
        stale_status.code(),
        StatusCode::KFailedPrecondition);

    ASSERT_TRUE(
        node_registry.GetNode(
            node_id,
            &stored).ok());

    EXPECT_EQ(
        stored.incarnation,
        2U);

    EXPECT_EQ(
        stored.worker_endpoint,
        "127.0.0.1:50052");

    // ========================================================
    // Delayed RemoveNode from incarnation 1.
    //
    // Must NOT remove incarnation 2.
    // ========================================================

    proto::v1::RemoveNodeRequest stale_remove;

    serialization::ToProto(
        node_id,
        stale_remove.mutable_node_id());

    stale_remove.set_incarnation(
        1);

    proto::v1::RemoveNodeResponse stale_remove_response;

    auto stale_remove_context =
        worker_client.CreateContext(
            std::chrono::seconds(2));

    const grpc::Status stale_remove_transport =
        stub->RemoveNode(
            stale_remove_context.get(),
            stale_remove,
            &stale_remove_response);

    ASSERT_TRUE(
        stale_remove_transport.ok());

    const Status stale_remove_status =
        serialization::FromProtoStatus(
            stale_remove_response.status());

    EXPECT_FALSE(
        stale_remove_status.ok());

    EXPECT_EQ(
        stale_remove_status.code(),
        StatusCode::KFailedPrecondition);

    // N1 / incarnation 2 must still exist.
    ASSERT_TRUE(
        node_registry.GetNode(
            node_id,
            &stored).ok());

    EXPECT_EQ(
        stored.incarnation,
        2U);

    // ========================================================
    // Correct RemoveNode(N1, 2)
    // ========================================================

    proto::v1::RemoveNodeRequest current_remove;

    serialization::ToProto(
        node_id,
        current_remove.mutable_node_id());

    current_remove.set_incarnation(
        2);

    proto::v1::RemoveNodeResponse current_remove_response;

    auto current_remove_context =
        worker_client.CreateContext(
            std::chrono::seconds(2));

    const grpc::Status current_remove_transport =
        stub->RemoveNode(
            current_remove_context.get(),
            current_remove,
            &current_remove_response);

    ASSERT_TRUE(
        current_remove_transport.ok());

    const Status current_remove_status =
        serialization::FromProtoStatus(
            current_remove_response.status());

    EXPECT_TRUE(
        current_remove_status.ok())
        << current_remove_status.ToString();

    EXPECT_FALSE(
        node_registry.GetNode(
            node_id,
            &stored).ok());

    ASSERT_TRUE(
        server.Shutdown().ok());
}

}  // namespace
}  // namespace dar
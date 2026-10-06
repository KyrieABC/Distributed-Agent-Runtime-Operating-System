/**
protobuf heartbeat
      ↓
real gRPC
      ↓
ControlService::Heartbeat
      ↓
ControlPlane
      ↓
NodeManager
      ↓
NodeRegistry
 */

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

namespace dar
{
namespace
{

TEST(
    Phase3HeartbeatTest,
    HeartbeatCrossesRpcBoundaryAndUpdatesRegistry)
{
    ConnectionPool connections;

    RpcClient rpc_client(
        connections);

    NodeRegistry registry;

    NodeManager node_manager(
        registry,
        rpc_client);

    ControlPlane control_plane(
        registry,
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
    // Register N1 directly.
    //
    // Registration RPC itself was already tested by 5D.
    // This test isolates the heartbeat path.
    // --------------------------------------------------------

    const NodeID node_id =
        NodeID::Random();

    ResourceSet capacity;

    ASSERT_TRUE(
        capacity.Set(
            "CPU",
            4.0).ok());

    NodeRecord node;

    node.node_id =
        node_id;

    node.worker_endpoint =
        "127.0.0.1:50051";

    node.total_resources =
        capacity;

    node.reported_available =
        capacity;

    node.incarnation =
        1;

    node.state =
        NodeState::kAlive;

    ASSERT_TRUE(
        registry.RegisterNode(
            std::move(node)).ok());

    // --------------------------------------------------------
    // Build heartbeat client.
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

    proto::v1::HeartbeatRequest request;

    proto::v1::NodeHeartbeat* heartbeat =
        request.mutable_heartbeat();

    serialization::ToProto(
        node_id,
        heartbeat->mutable_node_id());

    heartbeat->set_incarnation(
        1);

    ResourceSet available;

    ASSERT_TRUE(
        available.Set(
            "CPU",
            2.0).ok());

    serialization::ToProto(
        available,
        heartbeat->mutable_available_resources());

    heartbeat->set_running_tasks(
        2);

    // sent_at is intentionally optional for the Stage-5E
    // liveness path. Control-plane receipt time is authoritative.

    proto::v1::HeartbeatResponse response;

    auto context =
        worker_client.CreateContext(
            std::chrono::seconds(2));

    const grpc::Status transport_status =
        stub->Heartbeat(
            context.get(),
            request,
            &response);

    ASSERT_TRUE(
        transport_status.ok())
        << transport_status.error_message();

    const Status runtime_status =
        serialization::FromProtoStatus(
            response.status());

    ASSERT_TRUE(
        runtime_status.ok())
        << runtime_status.ToString();

    // --------------------------------------------------------
    // Verify registry update.
    // --------------------------------------------------------

    NodeRecord stored;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &stored).ok());

    EXPECT_EQ(
        stored.reported_available,
        available);

    EXPECT_EQ(
        stored.running_tasks,
        2U);

    EXPECT_NE(
        stored.last_heartbeat_received,
        std::chrono::steady_clock::time_point{});

    ASSERT_TRUE(
        server.Shutdown().ok());
}

TEST(
    Phase3HeartbeatTest,
    StaleHeartbeatCannotModifyNewIncarnation)
{
    ConnectionPool connections;

    RpcClient rpc_client(
        connections);

    NodeRegistry registry;

    NodeManager node_manager(
        registry,
        rpc_client);

    ControlPlane control_plane(
        registry,
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

    const NodeID node_id =
        NodeID::Random();

    ResourceSet capacity;

    ASSERT_TRUE(
        capacity.Set(
            "CPU",
            4.0).ok());

    NodeRecord node;

    node.node_id =
        node_id;

    node.worker_endpoint =
        "127.0.0.1:50052";

    node.total_resources =
        capacity;

    node.reported_available =
        capacity;

    node.incarnation =
        2;

    node.state =
        NodeState::kAlive;

    ASSERT_TRUE(
        registry.RegisterNode(
            std::move(node)).ok());

    ConnectionPool worker_connections;

    RpcClient worker_client(
        worker_connections);

    auto stub =
        proto::v1::ControlService::NewStub(
            worker_client.Channel(
                endpoint));

    proto::v1::HeartbeatRequest request;

    auto* heartbeat =
        request.mutable_heartbeat();

    serialization::ToProto(
        node_id,
        heartbeat->mutable_node_id());

    // OLD process.
    heartbeat->set_incarnation(
        1);

    ResourceSet stale_available;

    ASSERT_TRUE(
        stale_available.Set(
            "CPU",
            1.0).ok());

    serialization::ToProto(
        stale_available,
        heartbeat->mutable_available_resources());

    heartbeat->set_running_tasks(
        99);

    proto::v1::HeartbeatResponse response;

    auto context =
        worker_client.CreateContext(
            std::chrono::seconds(2));

    const grpc::Status transport_status =
        stub->Heartbeat(
            context.get(),
            request,
            &response);

    // Transport succeeded.
    ASSERT_TRUE(
        transport_status.ok());

    // DAR operation did not.
    const Status runtime_status =
        serialization::FromProtoStatus(
            response.status());

    EXPECT_FALSE(
        runtime_status.ok());

    EXPECT_EQ(
        runtime_status.code(),
        StatusCode::KFailedPrecondition);

    NodeRecord stored;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &stored).ok());

    // New incarnation is untouched.
    EXPECT_EQ(
        stored.incarnation,
        2U);

    EXPECT_EQ(
        stored.reported_available,
        capacity);

    EXPECT_EQ(
        stored.running_tasks,
        0U);

    ASSERT_TRUE(
        server.Shutdown().ok());
}


}  // namespace
}  // namespace dar
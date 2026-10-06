#include <gtest/gtest.h>

#include "dar/node/node_registry.h"

namespace dar
{
namespace
{

NodeRecord MakeNode(
    NodeID node_id,
    std::uint64_t incarnation,
    std::string endpoint = "127.0.0.1:50051")
{
    ResourceSet capacity;

    EXPECT_TRUE(
        capacity.Set(
            "CPU",
            1.0).ok());

    NodeRecord node;

    node.node_id =
        node_id;

    node.worker_endpoint =
        std::move(endpoint);

    node.total_resources =
        capacity;

    node.reported_available =
        capacity;

    node.incarnation =
        incarnation;

    node.state =
        NodeState::kAlive;

    return node;
}


TEST(
    Phase3NodeRegistryTest,
    DuplicateSameIncarnationIsIdempotent)
{
    NodeRegistry registry;

    const NodeID node_id =
        NodeID::Random();

    ASSERT_TRUE(
        registry.RegisterNode(
            MakeNode(
                node_id,
                1)).ok());

    // Same NodeID + same incarnation + same registration.
    EXPECT_TRUE(
        registry.RegisterNode(
            MakeNode(
                node_id,
                1)).ok());

    EXPECT_EQ(
        registry.Nodes().size(),
        1U);
}


TEST(
    Phase3NodeRegistryTest,
    NewerIncarnationReplacesOldLifetime)
{
    NodeRegistry registry;

    const NodeID node_id =
        NodeID::Random();

    ASSERT_TRUE(
        registry.RegisterNode(
            MakeNode(
                node_id,
                1,
                "127.0.0.1:50051")).ok());

    ASSERT_TRUE(
        registry.RegisterNode(
            MakeNode(
                node_id,
                2,
                "127.0.0.1:50052")).ok());

    NodeRecord stored;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &stored).ok());

    EXPECT_EQ(
        stored.incarnation,
        2U);

    EXPECT_EQ(
        stored.worker_endpoint,
        "127.0.0.1:50052");

    // Restart must not duplicate registration order.
    EXPECT_EQ(
        registry.Nodes().size(),
        1U);
}


TEST(
    Phase3NodeRegistryTest,
    OlderIncarnationIsRejected)
{
    NodeRegistry registry;

    const NodeID node_id =
        NodeID::Random();

    ASSERT_TRUE(
        registry.RegisterNode(
            MakeNode(
                node_id,
                2)).ok());

    const Status stale =
        registry.RegisterNode(
            MakeNode(
                node_id,
                1));

    EXPECT_FALSE(
        stale.ok());

    EXPECT_EQ(
        stale.code(),
        StatusCode::KFailedPrecondition);

    NodeRecord stored;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &stored).ok());

    EXPECT_EQ(
        stored.incarnation,
        2U);
}


TEST(
    Phase3NodeRegistryTest,
    ConflictingSameIncarnationIsRejected)
{
    NodeRegistry registry;

    const NodeID node_id =
        NodeID::Random();

    ASSERT_TRUE(
        registry.RegisterNode(
            MakeNode(
                node_id,
                1,
                "127.0.0.1:50051")).ok());

    const Status conflict =
        registry.RegisterNode(
            MakeNode(
                node_id,
                1,
                "127.0.0.1:50052"));

    EXPECT_FALSE(
        conflict.ok());

    EXPECT_EQ(
        conflict.code(),
        StatusCode::KFailedPrecondition);
}


TEST(
    Phase3NodeRegistryTest,
    RemoveRejectsStaleIncarnation)
{
    NodeRegistry registry;

    const NodeID node_id =
        NodeID::Random();

    ASSERT_TRUE(
        registry.RegisterNode(
            MakeNode(
                node_id,
                2)).ok());

    const Status stale_remove =
        registry.RemoveNode(
            node_id,
            1);

    EXPECT_FALSE(
        stale_remove.ok());

    EXPECT_EQ(
        stale_remove.code(),
        StatusCode::KFailedPrecondition);

    NodeRecord stored;

    EXPECT_TRUE(
        registry.GetNode(
            node_id,
            &stored).ok());

    EXPECT_EQ(
        stored.incarnation,
        2U);
}

TEST(
    Phase3NodeRegistryTest,
    HeartbeatUpdatesReportedNodeState)
{
    NodeRegistry registry;

    const NodeID node_id =
        NodeID::Random();

    ASSERT_TRUE(
        registry.RegisterNode(
            MakeNode(
                node_id,
                1)).ok());

    NodeRecord before;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &before).ok());

    ResourceSet reported;

    ASSERT_TRUE(
        reported.Set(
            "CPU",
            0.5).ok());

    ASSERT_TRUE(
        registry.Heartbeat(
            node_id,
            1,
            reported,
            3).ok());

    NodeRecord after;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &after).ok());

    EXPECT_EQ(
        after.reported_available,
        reported);

    EXPECT_EQ(
        after.running_tasks,
        3U);

    EXPECT_GT(
        after.last_heartbeat_received,
        before.last_heartbeat_received);
}

TEST(
    Phase3NodeRegistryTest,
    HeartbeatRejectsStaleIncarnation)
{
    NodeRegistry registry;

    const NodeID node_id =
        NodeID::Random();

    ASSERT_TRUE(
        registry.RegisterNode(
            MakeNode(
                node_id,
                2)).ok());

    ResourceSet reported;

    ASSERT_TRUE(
        reported.Set(
            "CPU",
            0.25).ok());

    const Status status =
        registry.Heartbeat(
            node_id,
            1,
            reported,
            7);

    EXPECT_FALSE(
        status.ok());

    EXPECT_EQ(
        status.code(),
        StatusCode::KFailedPrecondition);

    NodeRecord stored;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &stored).ok());

    EXPECT_EQ(
        stored.incarnation,
        2U);

    // Stale process must not mutate current lifetime.
    EXPECT_NE(
        stored.reported_available,
        reported);
}

TEST(
    Phase3NodeRegistryTest,
    HeartbeatRejectsUnknownNode)
{
    NodeRegistry registry;

    ResourceSet reported;

    ASSERT_TRUE(
        reported.Set(
            "CPU",
            1.0).ok());

    const Status status =
        registry.Heartbeat(
            NodeID::Random(),
            1,
            reported,
            0);

    EXPECT_FALSE(
        status.ok());

    EXPECT_EQ(
        status.code(),
        StatusCode::KNotFound);
}

TEST(
    Phase3NodeRegistryTest,
    HeartbeatDoesNotEraseControlPlaneReservations)
{
    NodeRegistry registry;

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
    // Control plane reserves 1 CPU.
    //
    // schedulable:
    //
    //     4 -> 3
    // --------------------------------------------------------

    ResourceSet requested_resources;

    ASSERT_TRUE(
        requested_resources.Set(
            "CPU",
            1.0).ok());

    ResourceRequest request(
        requested_resources);

    NodeRecord selected;

    ASSERT_TRUE(
        registry.ReserveFirstFit(
            request,
            &selected).ok());

    // --------------------------------------------------------
    // Imagine this heartbeat was produced from an older worker
    // snapshot and still claims all 4 CPUs are available.
    // --------------------------------------------------------

    ResourceSet worker_report;

    ASSERT_TRUE(
        worker_report.Set(
            "CPU",
            4.0).ok());

    ASSERT_TRUE(
        registry.Heartbeat(
            node_id,
            1,
            worker_report,
            0).ok());

    NodeRecord stored;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &stored).ok());

    // Worker observation changed.
    EXPECT_EQ(
        stored.reported_available,
        worker_report);

    // Control-plane reservation MUST remain.
    EXPECT_DOUBLE_EQ(
        stored.schedulable_available
            .Get("CPU")
            .ToDouble(),
        3.0);
}


}  // namespace
}  // namespace dar
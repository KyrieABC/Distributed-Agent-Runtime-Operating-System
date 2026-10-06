#include <chrono>

#include <gtest/gtest.h>

#include "dar/node/failure_detector.h"
#include "dar/node/node_registry.h"

namespace dar
{
namespace
{

NodeRecord MakeNode()
{
    ResourceSet capacity;

    EXPECT_TRUE(
        capacity.Set(
            "CPU",
            4.0).ok());

    NodeRecord node;

    node.node_id =
        NodeID::Random();

    node.worker_endpoint =
        "127.0.0.1:50051";

    node.total_resources =
        capacity;

    node.reported_available =
        capacity;

    node.incarnation = 1;

    node.state =
        NodeState::kAlive;

    return node;
}

TEST(
    Phase3FailureDetectorTest,
    AliveNodeRemainsAliveBeforeSuspectTimeout)
{
    NodeRegistry registry;

    NodeRecord node =
        MakeNode();

    const NodeID node_id =
        node.node_id;

    ASSERT_TRUE(
        registry.RegisterNode(
            std::move(node)).ok());

    NodeRecord registered;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &registered).ok());

    const auto base =
        registered.last_heartbeat_received;

    FailureDetectorOptions options;

    options.suspect_timeout =
        std::chrono::seconds(3);

    options.dead_timeout =
        std::chrono::seconds(10);

    FailureDetector detector(
        registry,
        options);

    detector.ScanOnce(
        base +
        std::chrono::seconds(2));

    NodeRecord observed;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &observed).ok());

    EXPECT_EQ(
        observed.state,
        NodeState::kAlive);
}

TEST(
    Phase3FailureDetectorTest,
    NodeBecomesSuspectAfterSuspectTimeout)
{
    NodeRegistry registry;

    NodeRecord node =
        MakeNode();

    const NodeID node_id =
        node.node_id;

    ASSERT_TRUE(
        registry.RegisterNode(
            std::move(node)).ok());

    NodeRecord registered;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &registered).ok());

    const auto base =
        registered.last_heartbeat_received;

    FailureDetector detector(
        registry);

    detector.ScanOnce(
        base +
        std::chrono::seconds(3));

    NodeRecord observed;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &observed).ok());

    EXPECT_EQ(
        observed.state,
        NodeState::kSuspect);
}

TEST(
    Phase3FailureDetectorTest,
    NodeBecomesDeadAfterDeadTimeout)
{
    NodeRegistry registry;

    NodeRecord node =
        MakeNode();

    const NodeID node_id =
        node.node_id;

    ASSERT_TRUE(
        registry.RegisterNode(
            std::move(node)).ok());

    NodeRecord registered;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &registered).ok());

    const auto base =
        registered.last_heartbeat_received;

    FailureDetector detector(
        registry);

    detector.ScanOnce(
        base +
        std::chrono::seconds(10));

    NodeRecord observed;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &observed).ok());

    EXPECT_EQ(
        observed.state,
        NodeState::kDead);
}

TEST(
    Phase3FailureDetectorTest,
    HeartbeatRevivesSuspectNode)
{
    NodeRegistry registry;

    NodeRecord node =
        MakeNode();

    const NodeID node_id =
        node.node_id;

    const std::uint64_t incarnation =
        node.incarnation;

    const ResourceSet available =
        node.reported_available;

    ASSERT_TRUE(
        registry.RegisterNode(
            std::move(node)).ok());

    NodeRecord registered;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &registered).ok());

    FailureDetector detector(
        registry);

    detector.ScanOnce(
        registered.last_heartbeat_received +
        std::chrono::seconds(3));

    NodeRecord suspect;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &suspect).ok());

    ASSERT_EQ(
        suspect.state,
        NodeState::kSuspect);

    ASSERT_TRUE(
        registry.Heartbeat(
            node_id,
            incarnation,
            available,
            0).ok());

    NodeRecord revived;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &revived).ok());

    EXPECT_EQ(
        revived.state,
        NodeState::kAlive);
}

TEST(
    Phase3FailureDetectorTest,
    DeadIncarnationCannotBeRevivedByHeartbeat)
{
    NodeRegistry registry;

    NodeRecord node =
        MakeNode();

    const NodeID node_id =
        node.node_id;

    const std::uint64_t incarnation =
        node.incarnation;

    const ResourceSet available =
        node.reported_available;

    ASSERT_TRUE(
        registry.RegisterNode(
            std::move(node)).ok());

    NodeRecord registered;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &registered).ok());

    FailureDetector detector(
        registry);

    detector.ScanOnce(
        registered.last_heartbeat_received +
        std::chrono::seconds(10));

    const Status status =
        registry.Heartbeat(
            node_id,
            incarnation,
            available,
            0);

    EXPECT_FALSE(
        status.ok());

    NodeRecord observed;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &observed).ok());

    EXPECT_EQ(
        observed.state,
        NodeState::kDead);
}

TEST(
    Phase3FailureDetectorTest,
    DeadNotificationOccursExactlyOnce)
{
    NodeRegistry registry;

    NodeRecord node =
        MakeNode();

    const NodeID node_id =
        node.node_id;

    ASSERT_TRUE(
        registry.RegisterNode(
            std::move(node)).ok());

    NodeRecord registered;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &registered).ok());

    FailureDetector detector(
        registry);

    int notifications = 0;

    detector.SetNodeDeadCallback(
        [&notifications](
            NodeID,
            std::uint64_t)
        {
            ++notifications;
        });

    detector.ScanOnce(
        registered.last_heartbeat_received +
        std::chrono::seconds(10));

    detector.ScanOnce(
        registered.last_heartbeat_received +
        std::chrono::seconds(20));

    EXPECT_EQ(
        notifications,
        1);
}

TEST(
    Phase3FailureDetectorTest,
    SuspectNodeReceivesNoNewPlacement)
{
    NodeRegistry registry;

    NodeRecord node =
        MakeNode();

    const NodeID node_id =
        node.node_id;

    ASSERT_TRUE(
        registry.RegisterNode(
            std::move(node)).ok());

    NodeRecord registered;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &registered).ok());

    FailureDetector detector(
        registry);

    detector.ScanOnce(
        registered.last_heartbeat_received +
        std::chrono::seconds(3));

    ResourceSet resources;

    ASSERT_TRUE(
        resources.Set(
            "CPU",
            1.0).ok());

    NodeRecord selected;

    const Status status =
        registry.ReserveFirstFit(
            ResourceRequest(
                std::move(resources)),
            &selected);

    EXPECT_FALSE(
        status.ok());

    NodeRecord observed;

    ASSERT_TRUE(
        registry.GetNode(
            node_id,
            &observed).ok());

    EXPECT_EQ(
        observed.state,
        NodeState::kSuspect);
}

}  // namespace
}  // namespace dar
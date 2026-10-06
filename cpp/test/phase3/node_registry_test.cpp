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

}  // namespace
}  // namespace dar
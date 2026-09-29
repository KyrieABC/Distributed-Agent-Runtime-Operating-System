#include <gtest/gtest.h>

#include "dar/common/id.h"
#include "dar/common/status.h"
#include "dar/core/resource.h"
#include "dar/serialization/serialization.h"

TEST(Phase3SerializationTest, TaskIDRoundTrip)
{
    const dar::TaskID original =
        dar::TaskID::Random();

    dar::proto::v1::TaskId wire;

    dar::serialization::ToProto(original, &wire);

    EXPECT_EQ(
        wire.value().size(),
        dar::TaskID::kSize
    );

    dar::TaskID decoded;

    const dar::Status status =
        dar::serialization::FromProto(
            wire,
            &decoded
        );

    ASSERT_TRUE(status.ok())
        << status.ToString();

    EXPECT_EQ(original, decoded);
}

TEST(Phase3SerializationTest, RejectsMalformedID)
{
    dar::proto::v1::TaskId wire;

    wire.set_value("too-short");

    dar::TaskID decoded;

    const dar::Status status =
        dar::serialization::FromProto(
            wire,
            &decoded
        );

    EXPECT_FALSE(status.ok());

    EXPECT_EQ(
        status.code(),
        dar::StatusCode::kInvalidArgument
    );
}

TEST(Phase3SerializationTest, RuntimeStatusRoundTrip)
{
    const dar::Status original =
        dar::Status::ResourceExhausted(
            "no CPU available"
        );

    dar::proto::v1::Status wire;

    dar::serialization::ToProto(
        original,
        &wire
    );

    const dar::Status decoded =
        dar::serialization::FromProtoStatus(wire);

    EXPECT_EQ(original, decoded);
}

TEST(Phase3SerializationTest, ResourceSetRoundTrip)
{
    dar::ResourceSet original;

    ASSERT_TRUE(
        original.Set("CPU", 2.5).ok()
    );

    ASSERT_TRUE(
        original.Set("GPU", 1.0).ok()
    );

    dar::proto::v1::ResourceSet wire;

    dar::serialization::ToProto(
        original,
        &wire
    );

    dar::ResourceSet decoded;

    const dar::Status status =
        dar::serialization::FromProto(
            wire,
            &decoded
        );

    ASSERT_TRUE(status.ok())
        << status.ToString();

    EXPECT_EQ(original, decoded);
}

TEST(Phase3SerializationTest, RejectsInvalidResource)
{
    dar::proto::v1::ResourceSet wire;

    (*wire.mutable_quantities())["CPU"] = -1.0;

    dar::ResourceSet decoded;

    const dar::Status status =
        dar::serialization::FromProto(
            wire,
            &decoded
        );

    EXPECT_FALSE(status.ok());
}
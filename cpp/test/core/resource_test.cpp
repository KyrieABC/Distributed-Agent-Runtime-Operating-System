#include "dar/core/resource.h"

#include <gtest/gtest.h>

namespace dar {
namespace {

TEST(ResourceTest, QuantityRoundTripsThroughDouble) {
  // First from double(0.1111)-> int64_t(11110)
    const auto quantity =
      ResourceQuantity::FromDouble(1.5);

  ASSERT_TRUE(quantity.has_value());

  // go back to original and see if equals
  EXPECT_DOUBLE_EQ(
      quantity->ToDouble(),
      1.5);
}


TEST(ResourceTest, SupportsMinimumFractionalPrecision) {
  const auto quantity =
      ResourceQuantity::FromDouble(0.0001);

  ASSERT_TRUE(quantity.has_value());

  EXPECT_DOUBLE_EQ(
      quantity->ToDouble(),
      0.0001);
}


// check if <0.0 work for FromDouble function
TEST(ResourceTest, RejectsNegativeQuantity) {
  const auto quantity =
      ResourceQuantity::FromDouble(-1.0);

  EXPECT_FALSE(quantity.has_value());
}


TEST(ResourceTest, MissingResourceReturnsZero) {
    // default map_:empty string, default ResourceQuantity(0)
    const ResourceSet resources;

  EXPECT_DOUBLE_EQ(
      resources.Get("CPU").ToDouble(),
      0.0);
}


TEST(ResourceTest, SettingZeroRemovesResource) {
  ResourceSet resources;

  ASSERT_TRUE(
      resources.Set("CPU", 4.0).ok());

  ASSERT_TRUE(
      resources.Set("CPU", 0.0).ok());

  EXPECT_DOUBLE_EQ(
      resources.Get("CPU").ToDouble(),
      0.0);

  EXPECT_TRUE(resources.values().empty());
}

// InvalidresourceName: emtpy(1 of them)
TEST(ResourceTest, RejectsInvalidResourceName) {
  ResourceSet resources;

  const Status status =
      resources.Set("", 1.0);

  EXPECT_EQ(
      status.code(),
      StatusCode::kInvalidArgument);

  EXPECT_TRUE(resources.values().empty());
}

// 
TEST(ResourceTest, CanFitValidRequest) {
  ResourceSet capacity;

  ASSERT_TRUE(
      capacity.Set("CPU", 4.0).ok());

  ASSERT_TRUE(
      capacity.Set("GPU", 1.0).ok());

    // pool now have 1 resourceSet: capacity -> available_:capacity, total_:capacity
  ResourcePool pool(capacity);

  ResourceSet requested;

  ASSERT_TRUE(
      requested.Set("CPU", 2.0).ok());

  ASSERT_TRUE(
      requested.Set("GPU", 0.5).ok());

      // compare available_(ResourceQuantity) term by term with requested(member variable of ResourceRequest) 
  EXPECT_TRUE(
      pool.CanFit(
          ResourceRequest(requested)));
}

// check what would happen if the size requested exceeds the available_
TEST(ResourceTest, CannotFitOversizedRequest) {
  ResourceSet capacity;

  ASSERT_TRUE(
      capacity.Set("CPU", 4.0).ok());

  ResourcePool pool(capacity);

  ResourceSet requested;

  ASSERT_TRUE(
      requested.Set("CPU", 5.0).ok());

  EXPECT_FALSE(
      pool.CanFit(
          ResourceRequest(requested)));
}


TEST(
    ResourceTest,
    AcquireAndReleasePreserveConservation) {
  ResourceSet capacity;

  ASSERT_TRUE(
      capacity.Set("CPU", 4.0).ok());

  ASSERT_TRUE(
      capacity.Set("GPU", 1.0).ok());

  ResourcePool pool(capacity);

  ResourceSet requested;

  ASSERT_TRUE(
      requested.Set("CPU", 1.5).ok());

  ASSERT_TRUE(
      requested.Set("GPU", 0.5).ok());

  ResourceRequest request(requested);

  ASSERT_TRUE(
      pool.Acquire(request).ok());

  EXPECT_DOUBLE_EQ(
      pool.Available()
          .Get("CPU")
          .ToDouble(),
      2.5);

  EXPECT_DOUBLE_EQ(
      pool.Available()
          .Get("GPU")
          .ToDouble(),
      0.5);

  ASSERT_TRUE(
      pool.Release(request).ok());

  EXPECT_EQ(
      pool.Available(),
      pool.Total());
}


TEST(ResourceTest, FailedAcquireIsAtomic) {
  ResourceSet capacity;

  ASSERT_TRUE(
      capacity.Set("CPU", 4.0).ok());

  ASSERT_TRUE(
      capacity.Set("GPU", 1.0).ok());

  ResourcePool pool(capacity);

  const ResourceSet before =
      pool.Available();

  ResourceSet requested;

  // CPU fits independently.
  ASSERT_TRUE(
      requested.Set("CPU", 2.0).ok());

  // GPU does not fit.
  ASSERT_TRUE(
      requested.Set("GPU", 2.0).ok());

  const Status status =
      pool.Acquire(
          ResourceRequest(requested));

  EXPECT_EQ(
      status.code(),
      StatusCode::KResourceExhausted);

  // Acquire must have all-or-nothing semantics.
  EXPECT_EQ(
      pool.Available(),
      before);
}


TEST(
    ResourceTest,
    CannotReleaseMoreThanWasAcquired) {
  ResourceSet capacity;

  ASSERT_TRUE(
      capacity.Set("CPU", 4.0).ok());

  ResourcePool pool(capacity);

  // Acquire one CPU.
  ResourceSet acquired;

  ASSERT_TRUE(
      acquired.Set("CPU", 1.0).ok());

  ASSERT_TRUE(
      pool.Acquire(
          ResourceRequest(acquired))
          .ok());

  // 4 total - 1 acquired = 3 available.
  ASSERT_DOUBLE_EQ(
      pool.Available()
          .Get("CPU")
          .ToDouble(),
      3.0);

  // Now incorrectly attempt to release two CPUs even though only
  // one CPU is currently acquired.
  ResourceSet over_release;

  ASSERT_TRUE(
      over_release.Set("CPU", 2.0).ok());

  const Status status =
      pool.Release(
          ResourceRequest(over_release));

  EXPECT_EQ(
      status.code(),
      StatusCode::KFailedPrecondition);

  // Failed release must not modify availability. Exactly one CPU
  // remains acquired.
  EXPECT_DOUBLE_EQ(
      pool.Available()
          .Get("CPU")
          .ToDouble(),
      3.0);
}


TEST(ResourceTest, DoubleReleaseIsRejected) {
  ResourceSet capacity;

  ASSERT_TRUE(
      capacity.Set("CPU", 4.0).ok());

  ResourcePool pool(capacity);

  ResourceSet resources;

  ASSERT_TRUE(
      resources.Set("CPU", 1.0).ok());

  const ResourceRequest request(resources);

  ASSERT_TRUE(
      pool.Acquire(request).ok());

  ASSERT_TRUE(
      pool.Release(request).ok());

  // The first release restored the complete capacity.
  EXPECT_DOUBLE_EQ(
      pool.Available()
          .Get("CPU")
          .ToDouble(),
      4.0);

  // Releasing the same allocation again is a double release.
  const Status status =
      pool.Release(request);

  EXPECT_EQ(
      status.code(),
      StatusCode::KFailedPrecondition);

  // The failed release must not create phantom capacity.
  EXPECT_DOUBLE_EQ(
      pool.Available()
          .Get("CPU")
          .ToDouble(),
      4.0);

  EXPECT_EQ(
      pool.Available(),
      pool.Total());
}

}  // namespace
}  // namespace dar

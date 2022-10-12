// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#include <string>

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/versioning_types.h"

namespace {

using fidl::Availability;
using fidl::Platform;
using fidl::Version;
using fidl::VersionRange;

VersionRange range(uint64_t x, uint64_t y) {
  return VersionRange(Version::From(x).value(), Version::From(y).value());
}

TEST(VersioningTypesTests, GoodPlatformParse) {
  EXPECT_EQ(Platform::Parse("foo123").value().name(), "foo123");
}

TEST(VersioningTypesTests, BadPlatformParseEmpty) { EXPECT_FALSE(Platform::Parse("").has_value()); }

TEST(VersioningTypesTests, BadPlatformParseInvalidChar) {
  EXPECT_FALSE(Platform::Parse("foo_bar").has_value());
}

TEST(VersioningTypesTests, GoodVersionFromMinNumeric) {
  auto maybe_version = Version::From(1);
  ASSERT_TRUE(maybe_version.has_value());
  EXPECT_EQ(maybe_version.value().ordinal(), 1);
  EXPECT_EQ(maybe_version.value().ToString(), "1");
}

TEST(VersioningTypesTests, GoodVersionFromMaxNumeric) {
  uint64_t ordinal = (1ull << 63) - 1;
  auto maybe_version = Version::From(ordinal);
  ASSERT_TRUE(maybe_version.has_value());
  EXPECT_EQ(maybe_version.value().ordinal(), ordinal);
  EXPECT_EQ(maybe_version.value().ToString(), std::to_string(ordinal));
  // Confirm this is in fact the last valid ordinal.
  EXPECT_EQ(Version::From(ordinal + 1), std::nullopt);
}

TEST(VersioningTypesTests, GoodVersionFromHead) {
  uint64_t ordinal = std::numeric_limits<uint64_t>::max();
  auto maybe_version = Version::From(ordinal);
  ASSERT_TRUE(maybe_version.has_value());
  EXPECT_EQ(maybe_version.value().ordinal(), ordinal);
  EXPECT_EQ(maybe_version.value().ToString(), "HEAD");
}

TEST(VersioningTypesTests, BadVersionFrom) {
  ASSERT_EQ(Version::From(0), std::nullopt);
  ASSERT_EQ(Version::From(1ull << 63), std::nullopt);
  ASSERT_EQ(Version::From(std::numeric_limits<uint64_t>::max() - 1), std::nullopt);
}

TEST(VersioningTypesTests, GoodVersionParse) {
  uint64_t max_numeric_ordinal = (1ull << 63) - 1;
  uint64_t head_ordinal = std::numeric_limits<uint64_t>::max();

  EXPECT_EQ(Version::Parse("1"), Version::From(1));
  EXPECT_EQ(Version::Parse(std::to_string(max_numeric_ordinal)),
            Version::From(max_numeric_ordinal));
  EXPECT_EQ(Version::Parse(std::to_string(head_ordinal)), Version::From(head_ordinal));
  EXPECT_EQ(Version::Parse("HEAD"), Version::From(head_ordinal));
}

TEST(VersioningTypesTests, BadVersionParse) {
  EXPECT_EQ(Version::Parse(""), std::nullopt);
  EXPECT_EQ(Version::Parse("0"), std::nullopt);
  EXPECT_EQ(Version::Parse("18446744073709551616"), std::nullopt);
  EXPECT_EQ(Version::Parse("-1"), std::nullopt);
}

TEST(VersioningTypesTests, GoodVersionRangeComparisons) {
  EXPECT_EQ(range(1, 2), range(1, 2));
  EXPECT_EQ(range(2, 3), range(2, 3));

  EXPECT_NE(range(1, 2), range(1, 3));
  EXPECT_NE(range(1, 3), range(2, 3));
  EXPECT_NE(range(2, 3), range(1, 2));

  EXPECT_LT(range(1, 2), range(1, 3));
  EXPECT_LT(range(1, 3), range(2, 3));
  EXPECT_LT(range(1, 2), range(2, 3));

  EXPECT_GT(range(1, 3), range(1, 2));
  EXPECT_GT(range(2, 3), range(1, 3));
  EXPECT_GT(range(2, 3), range(1, 2));
}

TEST(VersioningTypesTests, GoodVersionRangeIntersect) {
  // Case #1: (empty) (empty)
  EXPECT_EQ(VersionRange::Intersect(std::nullopt, std::nullopt), std::nullopt);

  // Case #2: (empty) |---|
  EXPECT_EQ(VersionRange::Intersect(std::nullopt, range(3, 6)), std::nullopt);

  // Case #3: |---| (empty)
  EXPECT_EQ(VersionRange::Intersect(range(3, 6), std::nullopt), std::nullopt);

  // Case #4:  |---|
  //                 |--|
  EXPECT_EQ(VersionRange::Intersect(range(3, 6), (range(7, 9))), std::nullopt);

  // Case #5:  |---|
  //               |--|
  EXPECT_EQ(VersionRange::Intersect(range(3, 6), (range(6, 8))), std::nullopt);

  // Case #6:  |---|
  //             |--|
  EXPECT_EQ(VersionRange::Intersect(range(3, 6), (range(5, 7))), range(5, 6));

  // Case #7:  |---|
  //            |--|
  EXPECT_EQ(VersionRange::Intersect(range(3, 6), (range(4, 6))), range(4, 6));

  // Case #8:  |---|
  //           |--|
  EXPECT_EQ(VersionRange::Intersect(range(3, 6), (range(3, 5))), range(3, 5));

  // Case #9:  |---|
  //            |-|
  EXPECT_EQ(VersionRange::Intersect(range(3, 6), (range(4, 5))), range(4, 5));

  // Case #10:  |---|
  //           |---|
  EXPECT_EQ(VersionRange::Intersect(range(3, 6), (range(3, 6))), range(3, 6));

  // Case #11:  |---|
  //          |--|
  EXPECT_EQ(VersionRange::Intersect(range(3, 6), (range(2, 4))), range(3, 4));

  // Case #12:  |---|
  //        |--|
  EXPECT_EQ(VersionRange::Intersect(range(3, 6), (range(1, 3))), std::nullopt);

  // Case #13: |---|
  //      |--|
  EXPECT_EQ(VersionRange::Intersect(range(3, 6), (range(1, 2))), std::nullopt);
}

TEST(VersioningTypesTests, GoodAvailabilityInitNone) {
  Availability availability;
  ASSERT_TRUE(availability.Init({}));
  EXPECT_EQ(availability.Debug(), "_ _ _");
}

TEST(VersioningTypesTests, GoodAvailabilityInitSome) {
  Availability availability;
  ASSERT_TRUE(availability.Init({.added = Version::From(1)}));
  EXPECT_EQ(availability.Debug(), "1 _ _");
}

TEST(VersioningTypesTests, GoodAvailabilityInitAll) {
  Availability availability;
  ASSERT_TRUE(availability.Init(
      {.added = Version::From(1), .deprecated = Version::From(2), .removed = Version::From(3)}));
  EXPECT_EQ(availability.Debug(), "1 2 3");
}

TEST(VersioningTypesTests, BadAvailabilityInitWrongOrder) {
  Availability availability;
  EXPECT_FALSE(availability.Init({.added = Version::From(1), .removed = Version::From(1)}));
}

TEST(VersioningTypesTests, GoodAvailabilityInheritUnbounded) {
  Availability availability;
  ASSERT_TRUE(availability.Init({}));
  ASSERT_TRUE(availability.Inherit(Availability::Unbounded()).Ok());
  EXPECT_EQ(availability.Debug(), "-inf _ +inf");
}

TEST(VersioningTypesTests, GoodAvailabilityInheritUnset) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init(
      {.added = Version::From(1), .deprecated = Version::From(2), .removed = Version::From(3)}));
  ASSERT_TRUE(child.Init({}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());
  ASSERT_TRUE(child.Inherit(parent).Ok());
  EXPECT_EQ(parent.Debug(), "1 2 3");
  EXPECT_EQ(child.Debug(), "1 2 3");
}

TEST(VersioningTypesTests, GoodAvailabilityInheritUnchanged) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init(
      {.added = Version::From(1), .deprecated = Version::From(2), .removed = Version::From(3)}));
  ASSERT_TRUE(child.Init(
      {.added = Version::From(1), .deprecated = Version::From(2), .removed = Version::From(3)}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());
  ASSERT_TRUE(child.Inherit(parent).Ok());
  EXPECT_EQ(parent.Debug(), "1 2 3");
  EXPECT_EQ(child.Debug(), "1 2 3");
}

TEST(VersioningTypesTests, GoodAvailabilityInheritPartial) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.added = Version::From(1)}));
  ASSERT_TRUE(child.Init({.removed = Version::From(2)}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());
  ASSERT_TRUE(child.Inherit(parent).Ok());
  EXPECT_EQ(parent.Debug(), "1 _ +inf");
  EXPECT_EQ(child.Debug(), "1 _ 2");
}

TEST(VersioningTypesTests, GoodAvailabilityInheritChangeDeprecation) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.added = Version::From(1), .deprecated = Version::From(1)}));
  ASSERT_TRUE(child.Init({.added = Version::From(2)}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());
  ASSERT_TRUE(child.Inherit(parent).Ok());
  EXPECT_EQ(parent.Debug(), "1 1 +inf");
  EXPECT_EQ(child.Debug(), "2 2 +inf");
}

TEST(VersioningTypesTests, GoodAvailabilityInheritEliminateDeprecation) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.added = Version::From(1), .deprecated = Version::From(2)}));
  ASSERT_TRUE(child.Init({.removed = Version::From(2)}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());
  ASSERT_TRUE(child.Inherit(parent).Ok());
  EXPECT_EQ(parent.Debug(), "1 2 +inf");
  EXPECT_EQ(child.Debug(), "1 _ 2");
}

TEST(VersioningTypesTests, BadAvailabilityInheritBeforeParentCompletely) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.added = Version::From(3)}));
  ASSERT_TRUE(child.Init(
      {.added = Version::From(1), .deprecated = Version::From(2), .removed = Version::From(3)}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());

  auto status = child.Inherit(parent);
  EXPECT_EQ(status.added, Availability::InheritResult::Status::kBeforeParentAdded);
  EXPECT_EQ(status.deprecated, Availability::InheritResult::Status::kBeforeParentAdded);
  EXPECT_EQ(status.removed, Availability::InheritResult::Status::kBeforeParentAdded);
}

TEST(VersioningTypesTests, BadAvailabilityInheritBeforeParentPartially) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.added = Version::From(3)}));
  ASSERT_TRUE(child.Init(
      {.added = Version::From(1), .deprecated = Version::From(2), .removed = Version::From(4)}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());

  auto status = child.Inherit(parent);
  EXPECT_EQ(status.added, Availability::InheritResult::Status::kBeforeParentAdded);
  EXPECT_EQ(status.deprecated, Availability::InheritResult::Status::kBeforeParentAdded);
  EXPECT_EQ(status.removed, Availability::InheritResult::Status::kOk);
}

TEST(VersioningTypesTests, BadAvailabilityInheritAfterParentCompletely) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.removed = Version::From(2)}));
  ASSERT_TRUE(child.Init(
      {.added = Version::From(2), .deprecated = Version::From(3), .removed = Version::From(4)}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());

  auto status = child.Inherit(parent);
  EXPECT_EQ(status.added, Availability::InheritResult::Status::kAfterParentRemoved);
  EXPECT_EQ(status.deprecated, Availability::InheritResult::Status::kAfterParentRemoved);
  EXPECT_EQ(status.removed, Availability::InheritResult::Status::kAfterParentRemoved);
}

TEST(VersioningTypesTests, BadAvailabilityInheritAfterParentPartially) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.removed = Version::From(2)}));
  ASSERT_TRUE(child.Init(
      {.added = Version::From(1), .deprecated = Version::From(2), .removed = Version::From(3)}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());

  auto status = child.Inherit(parent);
  EXPECT_EQ(status.added, Availability::InheritResult::Status::kOk);
  EXPECT_EQ(status.deprecated, Availability::InheritResult::Status::kAfterParentRemoved);
  EXPECT_EQ(status.removed, Availability::InheritResult::Status::kAfterParentRemoved);
}

TEST(VersioningTypesTests, BadAvailabilityInheritAfterParentDeprecated) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.deprecated = Version::From(2)}));
  ASSERT_TRUE(child.Init(
      {.added = Version::From(1), .deprecated = Version::From(3), .removed = Version::From(4)}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());

  auto status = child.Inherit(parent);
  EXPECT_EQ(status.added, Availability::InheritResult::Status::kOk);
  EXPECT_EQ(status.deprecated, Availability::InheritResult::Status::kAfterParentDeprecated);
  EXPECT_EQ(status.removed, Availability::InheritResult::Status::kOk);
}

TEST(VersioningTypesTests, GoodAvailabilityDecomposeWhole) {
  Availability availability;
  ASSERT_TRUE(availability.Init({.added = Version::From(1), .removed = Version::From(2)}));
  ASSERT_TRUE(availability.Inherit(Availability::Unbounded()).Ok());

  availability.Narrow(range(1, 2));
  EXPECT_EQ(availability.Debug(), "1 _ 2");
}

}  // namespace

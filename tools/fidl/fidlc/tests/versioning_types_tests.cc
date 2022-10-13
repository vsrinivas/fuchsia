// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <limits>
#include <string>

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/versioning_types.h"

namespace {

using fidl::Availability;
using Legacy = Availability::Legacy;
using InheritResult = Availability::InheritResult;
using Status = InheritResult::Status;
using LegacyStatus = InheritResult::LegacyStatus;
using fidl::Platform;
using fidl::Version;
using fidl::VersionRange;
using fidl::VersionSet;

Version v(uint64_t x) { return Version::From(x).value(); }
VersionRange range(uint64_t x, uint64_t y) { return VersionRange(v(x), v(y)); }
VersionSet set(uint64_t x, uint64_t y) { return VersionSet(range(x, y)); }
VersionSet set(std::pair<uint64_t, uint64_t> a, std::pair<uint64_t, uint64_t> b) {
  return VersionSet(range(a.first, a.second), range(b.first, b.second));
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
  uint64_t ordinal = std::numeric_limits<uint64_t>::max() - 1;
  auto maybe_version = Version::From(ordinal);
  ASSERT_TRUE(maybe_version.has_value());
  EXPECT_EQ(maybe_version.value().ordinal(), ordinal);
  EXPECT_EQ(maybe_version.value().ToString(), "HEAD");
}

TEST(VersioningTypesTests, GoodVersionFromLegacy) {
  uint64_t ordinal = std::numeric_limits<uint64_t>::max();
  auto maybe_version = Version::From(ordinal);
  ASSERT_TRUE(maybe_version.has_value());
  EXPECT_EQ(maybe_version.value().ordinal(), ordinal);
  EXPECT_EQ(maybe_version.value().ToString(), "LEGACY");
}

TEST(VersioningTypesTests, BadVersionFrom) {
  ASSERT_EQ(Version::From(0), std::nullopt);
  ASSERT_EQ(Version::From(1ull << 63), std::nullopt);
  ASSERT_EQ(Version::From(std::numeric_limits<uint64_t>::max() - 2), std::nullopt);
}

TEST(VersioningTypesTests, GoodVersionParse) {
  uint64_t max_numeric_ordinal = (1ull << 63) - 1;
  uint64_t head_ordinal = std::numeric_limits<uint64_t>::max() - 1;
  uint64_t legacy_ordinal = std::numeric_limits<uint64_t>::max();

  EXPECT_EQ(Version::Parse("1"), v(1));
  EXPECT_EQ(Version::Parse(std::to_string(max_numeric_ordinal)), v(max_numeric_ordinal));
  EXPECT_EQ(Version::Parse(std::to_string(head_ordinal)), v(head_ordinal));
  EXPECT_EQ(Version::Parse(std::to_string(legacy_ordinal)), v(legacy_ordinal));
  EXPECT_EQ(Version::Parse("HEAD"), v(head_ordinal));
  EXPECT_EQ(Version::Parse("LEGACY"), v(legacy_ordinal));
}

TEST(VersioningTypesTests, BadVersionParse) {
  EXPECT_EQ(Version::Parse(""), std::nullopt);
  EXPECT_EQ(Version::Parse("0"), std::nullopt);
  EXPECT_EQ(Version::Parse("9223372036854775808"), std::nullopt);   // 2^63
  EXPECT_EQ(Version::Parse("18446744073709551616"), std::nullopt);  // 2^64
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

TEST(VersioningTypesTests, GoodVersionSetContains) {
  VersionSet two_three(range(2, 4));
  EXPECT_FALSE(VersionSet(two_three).Contains(v(1)));
  EXPECT_TRUE(VersionSet(two_three).Contains(v(2)));
  EXPECT_TRUE(VersionSet(two_three).Contains(v(3)));
  EXPECT_FALSE(VersionSet(two_three).Contains(v(4)));
  EXPECT_FALSE(VersionSet(two_three).Contains(v(5)));
  EXPECT_FALSE(VersionSet(two_three).Contains(v(6)));

  VersionSet two_three_five(range(2, 4), range(5, 6));
  EXPECT_FALSE(two_three_five.Contains(v(1)));
  EXPECT_TRUE(two_three_five.Contains(v(2)));
  EXPECT_TRUE(two_three_five.Contains(v(3)));
  EXPECT_FALSE(two_three_five.Contains(v(4)));
  EXPECT_TRUE(two_three_five.Contains(v(5)));
  EXPECT_FALSE(two_three_five.Contains(v(6)));
}

TEST(VersioningTypesTests, GoodVersionSetIntersect) {
  // Case #1: (empty) (empty)
  EXPECT_EQ(VersionSet::Intersect(std::nullopt, std::nullopt), std::nullopt);

  // Case #2: |---| (empty)
  EXPECT_EQ(VersionSet::Intersect(set(1, 3), std::nullopt), std::nullopt);

  // Case #3: (empty) |---|
  EXPECT_EQ(VersionSet::Intersect(std::nullopt, set(1, 3)), std::nullopt);

  // Case #4: |---|
  //              |---|
  EXPECT_EQ(VersionSet::Intersect(set(1, 3), set(3, 5)), std::nullopt);

  // Case #5: |---|
  //          |---|
  EXPECT_EQ(VersionSet::Intersect(set(1, 3), set(1, 3)), set(1, 3));

  // Case #6: |---| |---|
  //                    |---|
  EXPECT_EQ(VersionSet::Intersect(set({1, 3}, {4, 6}), set(6, 8)), std::nullopt);

  // Case #7: |---| |---|
  //                |---|
  EXPECT_EQ(VersionSet::Intersect(set({1, 3}, {4, 6}), set(4, 6)), set(4, 6));

  // Case #8: |---| |---|
  //             |---|
  EXPECT_EQ(VersionSet::Intersect(set({1, 3}, {4, 6}), set(2, 5)), set({2, 3}, {4, 5}));

  // Case #9: |---| |---|
  //          |---|
  EXPECT_EQ(VersionSet::Intersect(set({1, 3}, {4, 6}), set(1, 3)), set(1, 3));

  // Case #10:           |---|
  //           |---| |---|
  EXPECT_EQ(VersionSet::Intersect(set(6, 8), set({1, 3}, {4, 6})), std::nullopt);

  // Case #11:       |---|
  //           |---| |---|
  EXPECT_EQ(VersionSet::Intersect(set(4, 6), set({1, 3}, {4, 6})), set(4, 6));

  // Case #12:    |---|
  //           |---| |---|
  EXPECT_EQ(VersionSet::Intersect(set(2, 5), set({1, 3}, {4, 6})), set({2, 3}, {4, 5}));

  // Case #13: |---|
  //           |---| |---|
  EXPECT_EQ(VersionSet::Intersect(set(1, 3), set({1, 3}, {4, 6})), set(1, 3));

  // Case #14: |---| |---|
  //                     |---| |---|
  EXPECT_EQ(VersionSet::Intersect(set({1, 3}, {4, 6}), set({6, 8}, {9, 11})), std::nullopt);

  // Case #15: |---| |---|
  //                 |---| |---|
  EXPECT_EQ(VersionSet::Intersect(set({1, 3}, {4, 6}), set({4, 6}, {7, 9})), set(4, 6));

  // Case #16: |---| |---|
  //              |---|  |---|
  EXPECT_EQ(VersionSet::Intersect(set({1, 3}, {4, 6}), set({2, 5}, {6, 8})), set({2, 3}, {4, 5}));

  // Case #17: |---| |---|
  //           |---| |---|
  EXPECT_EQ(VersionSet::Intersect(set({1, 3}, {4, 6}), set({1, 3}, {4, 6})), set({1, 3}, {4, 6}));

  // Case #18:    |---|  |---|
  //           |---| |---|
  EXPECT_EQ(VersionSet::Intersect(set({2, 5}, {6, 8}), set({1, 3}, {4, 6})), set({2, 3}, {4, 5}));
}

TEST(VersioningTypesTests, GoodAvailabilityInitNone) {
  Availability availability;
  ASSERT_TRUE(availability.Init({}));
  EXPECT_EQ(availability.Debug(), "_ _ _ _");
}

TEST(VersioningTypesTests, GoodAvailabilityInitAdded) {
  Availability availability;
  ASSERT_TRUE(availability.Init({.added = v(1)}));
  EXPECT_EQ(availability.Debug(), "1 _ _ _");
}

TEST(VersioningTypesTests, GoodAvailabilityInitLegacy) {
  Availability availability;
  ASSERT_TRUE(availability.Init({.legacy = Legacy::kYes}));
  EXPECT_EQ(availability.Debug(), "_ _ _ yes");
}

TEST(VersioningTypesTests, GoodAvailabilityInitAll) {
  Availability availability;
  ASSERT_TRUE(availability.Init(
      {.added = v(1), .deprecated = v(2), .removed = v(3), .legacy = Legacy::kNo}));
  EXPECT_EQ(availability.Debug(), "1 2 3 no");
}

TEST(VersioningTypesTests, BadAvailabilityInitWrongOrder) {
  Availability availability;
  EXPECT_FALSE(availability.Init({.added = v(1), .removed = v(1)}));
}

TEST(VersioningTypesTests, GoodAvailabilityInheritUnbounded) {
  Availability availability;
  ASSERT_TRUE(availability.Init({}));
  ASSERT_TRUE(availability.Inherit(Availability::Unbounded()).Ok());
  EXPECT_EQ(availability.Debug(), "-inf _ +inf n/a");
}

TEST(VersioningTypesTests, GoodAvailabilityInheritUnset) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.added = v(1), .deprecated = v(2), .removed = v(3)}));
  ASSERT_TRUE(child.Init({}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());
  ASSERT_TRUE(child.Inherit(parent).Ok());
  EXPECT_EQ(parent.Debug(), "1 2 3 no");
  EXPECT_EQ(child.Debug(), "1 2 3 no");
}

TEST(VersioningTypesTests, GoodAvailabilityInheritUnchanged) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.added = v(1), .deprecated = v(2), .removed = v(3)}));
  ASSERT_TRUE(child.Init({.added = v(1), .deprecated = v(2), .removed = v(3)}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());
  ASSERT_TRUE(child.Inherit(parent).Ok());
  EXPECT_EQ(parent.Debug(), "1 2 3 no");
  EXPECT_EQ(child.Debug(), "1 2 3 no");
}

TEST(VersioningTypesTests, GoodAvailabilityInheritPartial) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.added = v(1)}));
  ASSERT_TRUE(child.Init({.removed = v(2)}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());
  ASSERT_TRUE(child.Inherit(parent).Ok());
  EXPECT_EQ(parent.Debug(), "1 _ +inf n/a");
  EXPECT_EQ(child.Debug(), "1 _ 2 no");
}

TEST(VersioningTypesTests, GoodAvailabilityInheritChangeDeprecation) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.added = v(1), .deprecated = v(1)}));
  ASSERT_TRUE(child.Init({.added = v(2)}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());
  ASSERT_TRUE(child.Inherit(parent).Ok());
  EXPECT_EQ(parent.Debug(), "1 1 +inf n/a");
  EXPECT_EQ(child.Debug(), "2 2 +inf n/a");
}

TEST(VersioningTypesTests, GoodAvailabilityInheritEliminateDeprecation) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.added = v(1), .deprecated = v(2)}));
  ASSERT_TRUE(child.Init({.removed = v(2)}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());
  ASSERT_TRUE(child.Inherit(parent).Ok());
  EXPECT_EQ(parent.Debug(), "1 2 +inf n/a");
  EXPECT_EQ(child.Debug(), "1 _ 2 no");
}

TEST(VersioningTypesTests, GoodAvailabilityInheritLegacyRemovedAtSameTime) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.added = v(1), .removed = v(2), .legacy = Legacy::kYes}));
  ASSERT_TRUE(child.Init({.removed = v(2)}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());
  ASSERT_TRUE(child.Inherit(parent).Ok());
  EXPECT_EQ(parent.Debug(), "1 _ 2 yes");
  EXPECT_EQ(child.Debug(), "1 _ 2 yes");
}

TEST(VersioningTypesTests, GoodAvailabilityInheritLegacyRemovedEarlier) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.added = v(1), .removed = v(3), .legacy = Legacy::kYes}));
  ASSERT_TRUE(child.Init({.removed = v(2)}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());
  ASSERT_TRUE(child.Inherit(parent).Ok());
  EXPECT_EQ(parent.Debug(), "1 _ 3 yes");
  EXPECT_EQ(child.Debug(), "1 _ 2 no");
}

TEST(VersioningTypesTests, GoodAvailabilityInheritLegacyOverride) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.added = v(1), .removed = v(2), .legacy = Legacy::kYes}));
  ASSERT_TRUE(child.Init({.legacy = Legacy::kNo}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());
  ASSERT_TRUE(child.Inherit(parent).Ok());
  EXPECT_EQ(parent.Debug(), "1 _ 2 yes");
  EXPECT_EQ(child.Debug(), "1 _ 2 no");
}

TEST(VersioningTypesTests, GoodAvailabilityInheritLegacyExplicitNo) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.added = v(1), .removed = v(2), .legacy = Legacy::kNo}));
  ASSERT_TRUE(child.Init({.legacy = Legacy::kNo}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());
  ASSERT_TRUE(child.Inherit(parent).Ok());
  EXPECT_EQ(parent.Debug(), "1 _ 2 no");
  EXPECT_EQ(child.Debug(), "1 _ 2 no");
}

TEST(VersioningTypesTests, BadAvailabilityInheritBeforeParentCompletely) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.added = v(3)}));
  ASSERT_TRUE(child.Init({.added = v(1), .deprecated = v(2), .removed = v(3)}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());

  auto status = child.Inherit(parent);
  EXPECT_EQ(status.added, Status::kBeforeParentAdded);
  EXPECT_EQ(status.deprecated, Status::kBeforeParentAdded);
  EXPECT_EQ(status.removed, Status::kBeforeParentAdded);
  EXPECT_EQ(status.legacy, LegacyStatus::kOk);
}

TEST(VersioningTypesTests, BadAvailabilityInheritBeforeParentPartially) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.added = v(3)}));
  ASSERT_TRUE(child.Init({.added = v(1), .deprecated = v(2), .removed = v(4)}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());

  auto status = child.Inherit(parent);
  EXPECT_EQ(status.added, Status::kBeforeParentAdded);
  EXPECT_EQ(status.deprecated, Status::kBeforeParentAdded);
  EXPECT_EQ(status.removed, Status::kOk);
  EXPECT_EQ(status.legacy, LegacyStatus::kOk);
}

TEST(VersioningTypesTests, BadAvailabilityInheritAfterParentCompletely) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.added = v(1), .removed = v(2)}));
  ASSERT_TRUE(child.Init({.added = v(2), .deprecated = v(3), .removed = v(4)}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());

  auto status = child.Inherit(parent);
  EXPECT_EQ(status.added, Status::kAfterParentRemoved);
  EXPECT_EQ(status.deprecated, Status::kAfterParentRemoved);
  EXPECT_EQ(status.removed, Status::kAfterParentRemoved);
  EXPECT_EQ(status.legacy, LegacyStatus::kOk);
}

TEST(VersioningTypesTests, BadAvailabilityInheritAfterParentPartially) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.added = v(1), .removed = v(2)}));
  ASSERT_TRUE(child.Init({.added = v(1), .deprecated = v(2), .removed = v(3)}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());

  auto status = child.Inherit(parent);
  EXPECT_EQ(status.added, Status::kOk);
  EXPECT_EQ(status.deprecated, Status::kAfterParentRemoved);
  EXPECT_EQ(status.removed, Status::kAfterParentRemoved);
  EXPECT_EQ(status.legacy, LegacyStatus::kOk);
}

TEST(VersioningTypesTests, BadAvailabilityInheritAfterParentDeprecated) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.deprecated = v(2)}));
  ASSERT_TRUE(child.Init({.added = v(1), .deprecated = v(3), .removed = v(4)}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());

  auto status = child.Inherit(parent);
  EXPECT_EQ(status.added, Status::kOk);
  EXPECT_EQ(status.deprecated, Status::kAfterParentDeprecated);
  EXPECT_EQ(status.removed, Status::kOk);
  EXPECT_EQ(status.legacy, LegacyStatus::kOk);
}

TEST(VersioningTypesTests, BadAvailabilityInheritLegacyNoNeverRemoved) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({}));
  ASSERT_TRUE(child.Init({.legacy = Legacy::kNo}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());

  auto status = child.Inherit(parent);
  EXPECT_EQ(status.added, Status::kOk);
  EXPECT_EQ(status.deprecated, Status::kOk);
  EXPECT_EQ(status.removed, Status::kOk);
  EXPECT_EQ(status.legacy, LegacyStatus::kNeverRemoved);
}

TEST(VersioningTypesTests, BadAvailabilityInheritLegacyYesNeverRemoved) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({}));
  ASSERT_TRUE(child.Init({.legacy = Legacy::kYes}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());

  auto status = child.Inherit(parent);
  EXPECT_EQ(status.added, Status::kOk);
  EXPECT_EQ(status.deprecated, Status::kOk);
  EXPECT_EQ(status.removed, Status::kOk);
  EXPECT_EQ(status.legacy, LegacyStatus::kNeverRemoved);
}

TEST(VersioningTypesTests, BadAvailabilityInheritLegacyWithoutParent) {
  Availability parent, child;
  ASSERT_TRUE(parent.Init({.added = v(1), .removed = v(2)}));
  ASSERT_TRUE(child.Init({.legacy = Legacy::kYes}));
  ASSERT_TRUE(parent.Inherit(Availability::Unbounded()).Ok());

  auto status = child.Inherit(parent);
  EXPECT_EQ(status.added, Status::kOk);
  EXPECT_EQ(status.deprecated, Status::kOk);
  EXPECT_EQ(status.removed, Status::kOk);
  EXPECT_EQ(status.legacy, LegacyStatus::kWithoutParent);
}

TEST(VersioningTypesTests, GoodAvailabilityDecomposeWhole) {
  Availability availability;
  ASSERT_TRUE(availability.Init({.added = v(1), .removed = v(2)}));
  ASSERT_TRUE(availability.Inherit(Availability::Unbounded()).Ok());

  availability.Narrow(range(1, 2));
  EXPECT_EQ(availability.Debug(), "1 _ 2 no");
}

}  // namespace

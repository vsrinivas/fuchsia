// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <range/range.h>
#include <zircon/assert.h>
#include <zxtest/zxtest.h>

namespace range {
namespace {

TEST(LengthTest, One) {
    auto x = Range<uint64_t>(5, 6);
    EXPECT_EQ(x.length(), 1);
}

TEST(LengthTest, MoreThanOne) {
    auto x = Range<uint64_t>(5, 7);
    EXPECT_EQ(x.length(), 2);
}

TEST(LengthTest, Zero) {
    auto x = Range<uint64_t>(2, 1);
    EXPECT_EQ(x.length(), 0);
}

TEST(EqualToOperatorTest, EqualRanges) {
    auto x = Range<uint64_t>(5, 7);
    auto y = Range<uint64_t>(5, 7);
    EXPECT_EQ(x, y);
    EXPECT_TRUE(x == y);
}

TEST(EqualToOperatorTest, DifferentLengths) {
    auto x = Range<uint64_t>(5, 7);
    auto y = Range<uint64_t>(5, 5);
    EXPECT_FALSE(x == y);
}

TEST(EqualToOperatorTest, DifferentStarts) {
    auto x = Range<uint64_t>(3, 5);
    auto y = Range<uint64_t>(5, 7);
    EXPECT_FALSE(x == y);
}

TEST(EqualToOperatorTest, DifferentStartsDifferentLengths) {
    auto x = Range<uint64_t>(3, 5);
    auto y = Range<uint64_t>(5, 6);
    EXPECT_FALSE(x == y);
}

TEST(NotEqualToOperatorTest, EqualRanges) {
    auto x = Range<uint64_t>(5, 7);
    auto y = Range<uint64_t>(5, 7);
    EXPECT_FALSE(x != y);
}

TEST(NotEqualToOperatorTest, DifferentLengths) {
    auto x = Range<uint64_t>(5, 7);
    auto y = Range<uint64_t>(5, 5);
    EXPECT_NE(x, y);
    EXPECT_TRUE(x != y);
}

TEST(NotEqualToOperatorTest, DifferentStarts) {
    auto x = Range<uint64_t>(3, 5);
    auto y = Range<uint64_t>(5, 7);
    EXPECT_NE(x, y);
    EXPECT_TRUE(x != y);
}

TEST(NotEqualToOperatorTest, DifferentStartsDifferentLengths) {
    auto x = Range<uint64_t>(3, 5);
    auto y = Range<uint64_t>(5, 6);
    EXPECT_NE(x, y);
    EXPECT_TRUE(x != y);
}

TEST(OverlapTest, NonOverlapAdjuscentRanges) {
    // Two adjuscent but non-overlapping, ranges
    auto x = Range<uint64_t>(0, 1);
    auto y = Range<uint64_t>(1, 2);
    ASSERT_TRUE(Adjacent(x, y));
    ASSERT_TRUE(Adjacent(y, x));

    EXPECT_FALSE(Overlap(x, y));
    // Two adjuscent, non-overlapping, ranges in reverse order
    EXPECT_FALSE(Overlap(y, x));
}

TEST(OverlapTest, NonOverlapingNonAdjuscentRanges) {
    // Two non-overlapping ranges
    auto x = Range<uint64_t>(5, 7);
    auto y = Range<uint64_t>(9, 18);
    ASSERT_FALSE(Adjacent(x, y));
    ASSERT_FALSE(Adjacent(y, x));

    EXPECT_FALSE(Overlap(x, y));

    // Reverse the order
    EXPECT_FALSE(Overlap(y, x));
}

TEST(OverlapTest, OverlapingByOneNumber) {
    // Two ranges sharing just one number in common
    auto x = Range<uint64_t>(0, 2);
    auto y = Range<uint64_t>(1, 3);
    EXPECT_TRUE(Overlap(x, y));
    // Reverse the order
    EXPECT_TRUE(Overlap(y, x));
}

TEST(OverlapTest, OverlapingByMultipleNumbers) {
    // Two ranges sharing multiple numbers in common
    auto x = Range<uint64_t>(0, 4);
    auto y = Range<uint64_t>(2, 6);
    EXPECT_TRUE(Overlap(x, y));
    // Reverse the order
    EXPECT_TRUE(Overlap(y, x));
}

TEST(OverlapTest, OneRangeContainsTheOther) {
    // One range containing another
    auto x = Range<uint64_t>(0, 9);
    auto y = Range<uint64_t>(2, 5);
    EXPECT_TRUE(Overlap(x, y));
    // Reverse the order
    EXPECT_TRUE(Overlap(y, x));
}

TEST(OverlapTest, RangeOverlapsWithItself) {
    // Same range
    auto x = Range<uint64_t>(0, 9);
    EXPECT_TRUE(Overlap(x, x));
}

TEST(AdjacentTest, AdjacentRanges) {
    auto x = Range<uint64_t>(1, 3);
    auto y = Range<uint64_t>(3, 8);
    ASSERT_FALSE(Overlap(x, y));
    ASSERT_FALSE(Overlap(y, x));

    EXPECT_TRUE(Adjacent(x, y));
    EXPECT_TRUE(Adjacent(y, x));
}

TEST(AdjacentTest, NonAdjacentNonOverlapingRanges) {
    auto x = Range<uint64_t>(1, 3);
    auto y = Range<uint64_t>(5, 8);
    ASSERT_FALSE(Overlap(x, y));
    ASSERT_FALSE(Overlap(y, x));

    EXPECT_FALSE(Adjacent(x, y));
    EXPECT_FALSE(Adjacent(y, x));
}

TEST(AdjacentTest, NonAdjacentOverlapingRanges) {
    auto x = Range<uint64_t>(1, 5);
    auto y = Range<uint64_t>(4, 8);
    EXPECT_TRUE(Overlap(x, y));
    EXPECT_TRUE(Overlap(y, x));

    EXPECT_FALSE(Adjacent(x, y));
    EXPECT_FALSE(Adjacent(y, x));
}

TEST(MergableTest, AdjacentRanges) {
    auto x = Range<uint64_t>(1, 3);
    auto y = Range<uint64_t>(3, 8);
    ASSERT_FALSE(Overlap(x, y));
    ASSERT_FALSE(Overlap(y, x));
    ASSERT_TRUE(Adjacent(x, y));
    ASSERT_TRUE(Adjacent(y, x));

    EXPECT_TRUE(Mergable(x, y));
    // Reverse the order
    EXPECT_TRUE(Mergable(y, x));
}

TEST(MergableTest, NonAdjacentNonOverlapingRanges) {
    auto x = Range<uint64_t>(1, 3);
    auto y = Range<uint64_t>(5, 8);
    ASSERT_FALSE(Overlap(x, y));
    ASSERT_FALSE(Overlap(y, x));
    ASSERT_FALSE(Adjacent(x, y));
    ASSERT_FALSE(Adjacent(y, x));

    EXPECT_FALSE(Mergable(x, y));
    // Reverse the order
    EXPECT_FALSE(Mergable(y, x));
}

TEST(MergableTest, OverlapingRanges) {
    auto x = Range<uint64_t>(1, 5);
    auto y = Range<uint64_t>(4, 8);
    ASSERT_TRUE(Overlap(x, y));
    ASSERT_TRUE(Overlap(y, x));
    ASSERT_FALSE(Adjacent(x, y));
    ASSERT_FALSE(Adjacent(y, x));

    EXPECT_TRUE(Mergable(x, y));
    // Reverse the order
    EXPECT_TRUE(Mergable(y, x));
}

TEST(MergeTest, MergeAdjacentRanges) {
    auto x = Range<uint64_t>(1, 3);
    auto y = Range<uint64_t>(3, 8);
    ASSERT_TRUE(Adjacent(x, y));
    ASSERT_TRUE(Adjacent(y, x));
    ASSERT_FALSE(Overlap(x, y));
    ASSERT_FALSE(Overlap(y, x));
    ASSERT_TRUE(Mergable(x, y));
    ASSERT_TRUE(Mergable(y, x));

    auto merged = Merge(x, y);

    EXPECT_EQ(fit::result_state::ok, merged.state());
    EXPECT_EQ(merged.value().length(), x.length() + y.length());
    EXPECT_EQ(merged.value().start(), std::min(x.start(), y.start()));
    EXPECT_EQ(merged.value().end(), std::max(x.end(), y.end()));
}

TEST(MergeTest, TryMergeNonAdjacentNonOverlapingRanges) {
    auto x = Range<uint64_t>(1, 3);
    auto y = Range<uint64_t>(5, 8);
    ASSERT_FALSE(Adjacent(y, x));
    ASSERT_FALSE(Adjacent(x, y));
    ASSERT_FALSE(Overlap(y, x));
    ASSERT_FALSE(Overlap(x, y));
    ASSERT_FALSE(Mergable(x, y));

    auto merged = Merge(x, y);
    EXPECT_EQ(fit::result_state::error, merged.state());

    // Reverse the order
    merged = Merge(y, x);
    EXPECT_EQ(fit::result_state::error, merged.state());
}

TEST(MergeTest, OverlapingRanges) {
    auto x = Range<uint64_t>(1, 5);
    auto y = Range<uint64_t>(2, 8);
    auto result = Range<uint64_t>(1, 8);
    ASSERT_FALSE(Adjacent(y, x));
    ASSERT_FALSE(Adjacent(x, y));
    ASSERT_TRUE(Overlap(y, x));
    ASSERT_TRUE(Overlap(x, y));

    auto merged = Merge(x, y);
    EXPECT_EQ(fit::result_state::ok, merged.state());
    EXPECT_EQ(result, merged.value());

    // Reverse the order
    merged = Merge(y, x);
    EXPECT_EQ(fit::result_state::ok, merged.state());
    EXPECT_EQ(result, merged.value());
}

TEST(MergeTest, OverlapingByOneNumber) {
    // Two ranges sharing just one number in common
    auto x = Range<uint64_t>(0, 2);
    auto y = Range<uint64_t>(1, 3);
    auto result = Range<uint64_t>(0, 3);
    ASSERT_FALSE(Adjacent(x, y));
    ASSERT_FALSE(Adjacent(y, x));
    ASSERT_TRUE(Overlap(x, y));
    ASSERT_TRUE(Overlap(y, x));
    ASSERT_TRUE(Mergable(x, y));
    ASSERT_TRUE(Mergable(y, x));

    auto merged = Merge(x, y);
    EXPECT_EQ(fit::result_state::ok, merged.state());
    EXPECT_EQ(merged.value(), result);

    // Reverse the order
    merged = Merge(y, x);
    EXPECT_EQ(fit::result_state::ok, merged.state());
    EXPECT_TRUE(merged.value() == result);
}

TEST(MergeTest, OverlapingByMultipleNumbers) {
    // Two ranges sharing multiple numbers in common
    auto x = Range<uint64_t>(0, 3);
    auto y = Range<uint64_t>(1, 5);
    auto result = Range<uint64_t>(0, 5);
    ASSERT_FALSE(Adjacent(x, y));
    ASSERT_FALSE(Adjacent(y, x));
    ASSERT_TRUE(Overlap(x, y));
    ASSERT_TRUE(Overlap(y, x));
    ASSERT_TRUE(Mergable(x, y));
    ASSERT_TRUE(Mergable(y, x));

    auto merged = Merge(x, y);
    EXPECT_EQ(fit::result_state::ok, merged.state());
    EXPECT_EQ(merged.value(), result);
    // Reverse the order
    merged = Merge(y, x);
    EXPECT_EQ(fit::result_state::ok, merged.state());
    EXPECT_EQ(merged.value(), result);
}

TEST(MergeTest, OneRangeContainsTheOther) {
    // One range containing another
    auto x = Range<uint64_t>(0, 9);
    auto y = Range<uint64_t>(2, 5);
    ASSERT_FALSE(Adjacent(x, y));
    ASSERT_FALSE(Adjacent(y, x));
    ASSERT_TRUE(Overlap(x, y));
    ASSERT_TRUE(Overlap(y, x));
    ASSERT_TRUE(Mergable(x, y));
    ASSERT_TRUE(Mergable(y, x));

    auto merged = Merge(x, y);

    EXPECT_EQ(merged.value(), x);
    // Reverse the order
    merged = Merge(y, x);
    EXPECT_EQ(merged.value(), x);
}

TEST(MergeTest, MergeWithItself) {
    // Same range
    auto x = Range<uint64_t>(0, 10);
    ASSERT_FALSE(Adjacent(x, x));
    ASSERT_TRUE(Overlap(x, x));
    ASSERT_TRUE(Mergable(x, x));
    ASSERT_TRUE(Mergable(x, x));

    auto merged = Merge(x, x);
    EXPECT_EQ(fit::result_state::ok, merged.state());
    EXPECT_EQ(merged.value(), x);
}

} // namespace

} // namespace range

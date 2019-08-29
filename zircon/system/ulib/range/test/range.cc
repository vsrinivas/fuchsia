// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/assert.h>

#include <range/range.h>
#include <zxtest/zxtest.h>

namespace range {
namespace {

TEST(LengthTest, One) {
  auto x = Range<uint64_t>(5, 6);
  EXPECT_EQ(x.Length(), 1);
}

TEST(LengthTest, MoreThanOne) {
  auto x = Range<uint64_t>(5, 7);
  EXPECT_EQ(x.Length(), 2);
}

TEST(LengthTest, Zero) {
  auto x = Range<uint64_t>(2, 1);
  EXPECT_EQ(x.Length(), 0);
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

  auto merged = x;
  ASSERT_OK(merged.Merge(y));

  EXPECT_EQ(merged.Length(), x.Length() + y.Length());
  EXPECT_EQ(merged.Start(), std::min(x.Start(), y.Start()));
  EXPECT_EQ(merged.End(), std::max(x.End(), y.End()));
}

TEST(MergeTest, TryMergeNonAdjacentNonOverlapingRanges) {
  auto x = Range<uint64_t>(1, 3);
  auto y = Range<uint64_t>(5, 8);
  ASSERT_FALSE(Adjacent(y, x));
  ASSERT_FALSE(Adjacent(x, y));
  ASSERT_FALSE(Overlap(y, x));
  ASSERT_FALSE(Overlap(x, y));
  ASSERT_FALSE(Mergable(x, y));

  auto merged = x;
  EXPECT_STATUS(ZX_ERR_OUT_OF_RANGE, merged.Merge(y));

  // Reverse the order
  merged = y;
  EXPECT_STATUS(ZX_ERR_OUT_OF_RANGE, merged.Merge(x));
}

TEST(MergeTest, OverlapingRanges) {
  auto x = Range<uint64_t>(1, 5);
  auto y = Range<uint64_t>(2, 8);
  auto result = Range<uint64_t>(1, 8);
  ASSERT_FALSE(Adjacent(y, x));
  ASSERT_FALSE(Adjacent(x, y));
  ASSERT_TRUE(Overlap(y, x));
  ASSERT_TRUE(Overlap(x, y));

  auto merged = x;
  ASSERT_OK(merged.Merge(y));
  EXPECT_EQ(result, merged);

  // Reverse the order
  merged = y;
  ASSERT_OK(merged.Merge(x));
  EXPECT_EQ(result, merged);
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

  auto merged = x;
  ASSERT_OK(merged.Merge(y));
  EXPECT_EQ(merged, result);

  // Reverse the order
  merged = y;
  ASSERT_OK(merged.Merge(x));
  EXPECT_TRUE(merged == result);
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

  auto merged = x;
  ASSERT_OK(merged.Merge(y));
  EXPECT_EQ(merged, result);
  // Reverse the order
  merged = y;
  ASSERT_OK(merged.Merge(x));
  EXPECT_EQ(merged, result);
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

  auto merged = x;
  ASSERT_OK(merged.Merge(y));

  EXPECT_EQ(merged, x);
  // Reverse the order
  merged = y;
  ASSERT_OK(merged.Merge(x));
  EXPECT_EQ(merged, x);
}

TEST(MergeTest, MergeWithItself) {
  // Same range
  auto x = Range<uint64_t>(0, 10);
  ASSERT_FALSE(Adjacent(x, x));
  ASSERT_TRUE(Overlap(x, x));
  ASSERT_TRUE(Mergable(x, x));
  ASSERT_TRUE(Mergable(x, x));

  auto merged = x;
  ASSERT_OK(merged.Merge(x));
  EXPECT_EQ(merged, x);
}

TEST(ContainsTest, ContainingRange) {
  ASSERT_TRUE(Contains(Range<uint64_t>(1, 10), Range<uint64_t>(4, 8)));
}

TEST(ContainsTest, SelfContained) {
  ASSERT_TRUE(Contains(Range<uint64_t>(1, 10), Range<uint64_t>(1, 10)));
}

TEST(ContainsTest, ContainedLargerThanContainer) {
  ASSERT_FALSE(Contains(Range<uint64_t>(4, 8), Range<uint64_t>(1, 10)));
}

TEST(ContainsTest, ContainerEndSmallerThanContainedEnd) {
  ASSERT_FALSE(Contains(Range<uint64_t>(1, 10), Range<uint64_t>(5, 11)));
}

TEST(ContainsTest, ContainerStartLargerThanContainedStart) {
  ASSERT_FALSE(Contains(Range<uint64_t>(4, 8), Range<uint64_t>(1, 5)));
}

TEST(CustomRangeTest, CustomKey) {
  Range<uint32_t> range(0, 10);
  EXPECT_EQ(0, range.Start());
  EXPECT_EQ(10, range.End());
}

TEST(CustomRangeTest, CustomContainer) {
  struct Container {
    uint64_t Start() const { return start_; }
    uint64_t End() const { return end_; }

    uint64_t other_data_ = 0;
    uint64_t start_ = 0;
    uint64_t end_ = 0;
  };

  Container c = {};
  c.start_ = 5;
  c.end_ = 10;

  Range<uint64_t, Container> range(std::move(c));
  EXPECT_EQ(5, range.Start());
  EXPECT_EQ(10, range.End());

  c = range.release();
  EXPECT_EQ(5, c.start_);
  EXPECT_EQ(10, c.end_);
}

TEST(CustomRangeTest, CustomContainerTraits) {
  struct Container {
    // Note: No "Start()" or "End()" methods.
    uint64_t start_ = 0;
    uint64_t end_ = 0;
  };

  struct Traits {
    static uint64_t Start(const Container& obj) { return obj.start_; }
    static uint64_t End(const Container& obj) { return obj.end_; }
    static zx_status_t Update(const Container* other, uint64_t start, uint64_t end,
                              Container* obj) {
      obj->start_ = start;
      obj->end_ = end;
      return ZX_OK;
    }
  };
  using RangeWithTraits = Range<uint64_t, Container, Traits>;

  Container c = {};
  c.start_ = 5;
  c.end_ = 10;
  RangeWithTraits range1(std::move(c));

  EXPECT_EQ(5, range1.Start());
  EXPECT_EQ(10, range1.End());

  c = {};
  c.start_ = 0;
  c.end_ = 5;
  RangeWithTraits range2(std::move(c));
  EXPECT_TRUE(Adjacent(range1, range2));

  ASSERT_OK(range1.Merge(range2));
  c = range1.release();
  // Observe that the ranges merged.
  EXPECT_EQ(0, c.start_);
  EXPECT_EQ(10, c.end_);
}

TEST(CustomRangeTest, RejectedMergesDoNotModifyRange) {
  struct Container {
    uint64_t start_ = 0;
    uint64_t end_ = 0;
  };

  struct Traits {
    static uint64_t Start(const Container& obj) { return obj.start_; }
    static uint64_t End(const Container& obj) { return obj.end_; }
    static zx_status_t Update(const Container* other, uint64_t start, uint64_t end,
                              Container* obj) {
      return ZX_ERR_INTERNAL;
    }
  };
  using RangeWithTraits = Range<uint64_t, Container, Traits>;

  Container c1, c2 = {}, c3;
  c1.start_ = 0;
  c1.end_ = 5;
  c2.start_ = 5;
  c2.end_ = 10;
  c3.start_ = 1;
  c3.end_ = 3;
  RangeWithTraits range1(std::move(c1));
  RangeWithTraits range2(std::move(c2));
  RangeWithTraits range3(std::move(c3));

  EXPECT_TRUE(Adjacent(range1, range2));
  EXPECT_TRUE(Mergable(range1, range2));
  EXPECT_TRUE(Contains(range1, range3));
  EXPECT_FALSE(Contains(range2, range3));

  ASSERT_STATUS(ZX_ERR_INTERNAL, range1.Merge(range2));

  EXPECT_EQ(0, range1.Start());
  EXPECT_EQ(5, range1.End());
  EXPECT_EQ(5, range2.Start());
  EXPECT_EQ(10, range2.End());
}

}  // namespace
}  // namespace range

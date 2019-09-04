// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "range/interval-tree.h"

#include <zxtest/zxtest.h>

namespace range {
namespace {

using TestRange = range::Range<uint64_t>;
using TestIntervalTree = IntervalTree<TestRange>;

TEST(IntervalTreeTest, EmptyTreeContainsNoRanges) {
  TestIntervalTree tree;
  ASSERT_TRUE(tree.empty());
  ASSERT_EQ(0, tree.size());

  EXPECT_EQ(tree.end(), tree.find(0));
  EXPECT_EQ(tree.end(), tree.find(1));
  EXPECT_EQ(tree.end(), tree.find(10000));
}

TEST(IntervalTreeTest, InsertOneRangeIncreasesTreeSizeByOne) {
  TestIntervalTree tree;
  tree.insert(TestRange(1, 2));
  EXPECT_EQ(1, tree.size());
}

TEST(IntervalTreeTest, FindReturnsInsertedRange) {
  TestIntervalTree tree;
  tree.insert(TestRange(1, 2));
  EXPECT_EQ(tree.end(), tree.find(0));
  EXPECT_EQ(TestRange(1, 2), tree.find(1)->second);
  EXPECT_EQ(tree.end(), tree.find(2));
  EXPECT_EQ(1, tree.size());
}

TEST(IntervalTreeTest, InsertAdjacentAfterPriorRangeCausesMerge) {
  TestIntervalTree tree;
  tree.insert(TestRange(0, 1));
  EXPECT_EQ(TestRange(0, 1), tree.find(0)->second);
  tree.insert(TestRange(1, 2));
  EXPECT_EQ(TestRange(0, 2), tree.find(0)->second);
  tree.insert(TestRange(2, 3));
  EXPECT_EQ(TestRange(0, 3), tree.find(0)->second);
  EXPECT_EQ(1, tree.size());
}

TEST(IntervalTreeTest, InsertAdjacentBeforePriorRangeCausesMerge) {
  TestIntervalTree tree;
  tree.insert(TestRange(2, 3));
  EXPECT_EQ(TestRange(2, 3), tree.find(2)->second);
  tree.insert(TestRange(1, 2));
  EXPECT_EQ(TestRange(1, 3), tree.find(1)->second);
  tree.insert(TestRange(0, 1));
  EXPECT_EQ(TestRange(0, 3), tree.find(0)->second);
  EXPECT_EQ(1, tree.size());
}

TEST(IntervalTreeTest, InsertAdjacentBetweenPriorRangesCausesThreeWayMerge) {
  TestIntervalTree tree;
  // Setup tree
  tree.insert(TestRange(1, 3));
  tree.insert(TestRange(5, 7));

  // Verify preconditions
  ASSERT_EQ(tree.end(), tree.find(0));
  ASSERT_EQ(TestRange(1, 3), tree.find(1)->second);
  ASSERT_EQ(TestRange(1, 3), tree.find(2)->second);
  ASSERT_EQ(tree.end(), tree.find(3));
  ASSERT_EQ(tree.end(), tree.find(4));
  ASSERT_EQ(TestRange(5, 7), tree.find(5)->second);
  ASSERT_EQ(TestRange(5, 7), tree.find(6)->second);
  ASSERT_EQ(tree.end(), tree.find(7));
  ASSERT_EQ(2, tree.size());

  // Insert range that exactly fits between the two ranges
  tree.insert(TestRange(3, 5));

  // Verify postconditions
  EXPECT_EQ(tree.end(), tree.find(0));
  EXPECT_EQ(TestRange(1, 7), tree.find(1)->second);
  EXPECT_EQ(TestRange(1, 7), tree.find(2)->second);
  EXPECT_EQ(TestRange(1, 7), tree.find(3)->second);
  EXPECT_EQ(TestRange(1, 7), tree.find(4)->second);
  EXPECT_EQ(TestRange(1, 7), tree.find(5)->second);
  EXPECT_EQ(TestRange(1, 7), tree.find(6)->second);
  EXPECT_EQ(tree.end(), tree.find(7));
  EXPECT_EQ(1, tree.size());
}

TEST(IntervalTreeTest, InsertOverlapIntersectStartExtendsRange) {
  TestIntervalTree tree;
  tree.insert(TestRange(1, 3));

  // Verify preconditions
  ASSERT_EQ(tree.end(), tree.find(0));
  ASSERT_EQ(TestRange(1, 3), tree.find(1)->second);
  ASSERT_EQ(TestRange(1, 3), tree.find(2)->second);
  ASSERT_EQ(tree.end(), tree.find(3));
  ASSERT_EQ(1, tree.size());

  // Insert range that overlaps the current range.
  tree.insert(TestRange(1, 4));

  // Verify postconditions
  EXPECT_EQ(tree.end(), tree.find(0));
  EXPECT_EQ(TestRange(1, 4), tree.find(1)->second);
  EXPECT_EQ(TestRange(1, 4), tree.find(2)->second);
  EXPECT_EQ(TestRange(1, 4), tree.find(3)->second);
  EXPECT_EQ(tree.end(), tree.find(4));
  EXPECT_EQ(1, tree.size());
}

TEST(IntervalTreeTest, InsertOverlapWithEndOfPriorRangeExtendsRange) {
  TestIntervalTree tree;
  tree.insert(TestRange(1, 3));

  // Verify preconditions
  ASSERT_EQ(tree.end(), tree.find(0));
  ASSERT_EQ(TestRange(1, 3), tree.find(1)->second);
  ASSERT_EQ(TestRange(1, 3), tree.find(2)->second);
  ASSERT_EQ(tree.end(), tree.find(3));
  ASSERT_EQ(1, tree.size());

  // Insert range that overlaps the prior range
  tree.insert(TestRange(2, 4));

  // Verify postconditions
  EXPECT_EQ(tree.end(), tree.find(0));
  EXPECT_EQ(TestRange(1, 4), tree.find(1)->second);
  EXPECT_EQ(TestRange(1, 4), tree.find(2)->second);
  EXPECT_EQ(TestRange(1, 4), tree.find(3)->second);
  EXPECT_EQ(tree.end(), tree.find(4));
  EXPECT_EQ(1, tree.size());
}

TEST(IntervalTreeTest, InsertOverlapWithStartOfPriorRangePreExtendsRange) {
  TestIntervalTree tree;
  tree.insert(TestRange(2, 4));

  // Verify preconditions
  ASSERT_EQ(tree.end(), tree.find(1));
  ASSERT_EQ(TestRange(2, 4), tree.find(2)->second);
  ASSERT_EQ(TestRange(2, 4), tree.find(3)->second);
  ASSERT_EQ(tree.end(), tree.find(4));
  ASSERT_EQ(1, tree.size());

  // Insert range that overlaps the prior range
  tree.insert(TestRange(1, 3));

  // Verify postconditions
  EXPECT_EQ(tree.end(), tree.find(0));
  EXPECT_EQ(TestRange(1, 4), tree.find(1)->second);
  EXPECT_EQ(TestRange(1, 4), tree.find(2)->second);
  EXPECT_EQ(TestRange(1, 4), tree.find(3)->second);
  EXPECT_EQ(tree.end(), tree.find(4));
  EXPECT_EQ(1, tree.size());
}

TEST(IntervalTreeTest, InsertOverlapBetweenTwoPriorRangesCausesThreeWayMerge) {
  TestIntervalTree tree;
  // Setup tree
  tree.insert(TestRange(1, 3));
  tree.insert(TestRange(5, 7));

  // Verify preconditions
  ASSERT_EQ(tree.end(), tree.find(0));
  ASSERT_EQ(TestRange(1, 3), tree.find(1)->second);
  ASSERT_EQ(TestRange(1, 3), tree.find(2)->second);
  ASSERT_EQ(tree.end(), tree.find(3));
  ASSERT_EQ(tree.end(), tree.find(4));
  ASSERT_EQ(TestRange(5, 7), tree.find(5)->second);
  ASSERT_EQ(TestRange(5, 7), tree.find(6)->second);
  ASSERT_EQ(tree.end(), tree.find(7));
  ASSERT_EQ(2, tree.size());

  // Insert range that exactly overlaps the two ranges.
  tree.insert(TestRange(2, 6));

  // Verify postconditions
  EXPECT_EQ(tree.end(), tree.find(0));
  EXPECT_EQ(TestRange(1, 7), tree.find(1)->second);
  EXPECT_EQ(TestRange(1, 7), tree.find(2)->second);
  EXPECT_EQ(TestRange(1, 7), tree.find(3)->second);
  EXPECT_EQ(TestRange(1, 7), tree.find(4)->second);
  EXPECT_EQ(TestRange(1, 7), tree.find(5)->second);
  EXPECT_EQ(TestRange(1, 7), tree.find(6)->second);
  EXPECT_EQ(tree.end(), tree.find(7));
  EXPECT_EQ(1, tree.size());
}

TEST(IntervalTreeTest, InsertSubsumingTwoPriorRangesCausesThreeWayMerge) {
  TestIntervalTree tree;
  // Setup tree
  tree.insert(TestRange(2, 4));
  tree.insert(TestRange(5, 7));

  // Verify preconditions
  ASSERT_EQ(tree.end(), tree.find(0));
  ASSERT_EQ(tree.end(), tree.find(1));
  ASSERT_EQ(TestRange(2, 4), tree.find(2)->second);
  ASSERT_EQ(TestRange(2, 4), tree.find(3)->second);
  ASSERT_EQ(tree.end(), tree.find(4));
  ASSERT_EQ(TestRange(5, 7), tree.find(5)->second);
  ASSERT_EQ(TestRange(5, 7), tree.find(6)->second);
  ASSERT_EQ(tree.end(), tree.find(7));
  ASSERT_EQ(2, tree.size());

  // Insert range that entirely overlaps the two prior requests.
  tree.insert(TestRange(1, 8));

  // Verify postconditions
  EXPECT_EQ(1, tree.size());
  EXPECT_EQ(tree.end(), tree.find(0));
  EXPECT_EQ(TestRange(1, 8), tree.find(1)->second);
  EXPECT_EQ(TestRange(1, 8), tree.find(2)->second);
  EXPECT_EQ(TestRange(1, 8), tree.find(3)->second);
  EXPECT_EQ(TestRange(1, 8), tree.find(4)->second);
  EXPECT_EQ(TestRange(1, 8), tree.find(5)->second);
  EXPECT_EQ(TestRange(1, 8), tree.find(6)->second);
  EXPECT_EQ(TestRange(1, 8), tree.find(7)->second);
  EXPECT_EQ(tree.end(), tree.find(8));
}

TEST(IntervalTreeTest, InsertSubsumingManyPriorRangesCausesManyWayMerge) {
  TestIntervalTree tree;
  // Setup tree
  tree.insert(TestRange(2, 4));
  tree.insert(TestRange(5, 7));
  tree.insert(TestRange(9, 10));
  tree.insert(TestRange(12, 14));

  // Verify preconditions
  ASSERT_EQ(4, tree.size());

  // Insert range that entirely overlaps all prior requests.
  tree.insert(TestRange(1, 15));

  // Verify postconditions
  EXPECT_EQ(tree.end(), tree.find(0));
  EXPECT_EQ(TestRange(1, 15), tree.find(1)->second);
  EXPECT_EQ(tree.end(), tree.find(15));
  EXPECT_EQ(1, tree.size());
}

TEST(IntervalTreeTest, InsertAlignedAtStartOfRangeSubsumingManyPriorRangesCausesManyWayMerge) {
  TestIntervalTree tree;
  // Setup tree
  tree.insert(TestRange(2, 4));
  tree.insert(TestRange(5, 7));
  tree.insert(TestRange(9, 10));
  tree.insert(TestRange(12, 14));

  // Verify preconditions
  ASSERT_EQ(4, tree.size());

  // Insert range that entirely overlaps all prior requests.
  tree.insert(TestRange(2, 15));

  // Verify postconditions
  EXPECT_EQ(1, tree.size());
  EXPECT_EQ(tree.end(), tree.find(1));
  EXPECT_EQ(TestRange(2, 15), tree.find(2)->second);
  EXPECT_EQ(tree.end(), tree.find(15));
}

TEST(IntervalTreeTest, EraseEntireRangeDeletesItCompletely) {
  TestIntervalTree tree;
  tree.insert(TestRange(2, 3));
  ASSERT_EQ(TestRange(2, 3), tree.find(2)->second);

  tree.erase(2);

  EXPECT_EQ(tree.end(), tree.find(1));
  EXPECT_EQ(tree.end(), tree.find(2));
  EXPECT_EQ(tree.end(), tree.find(3));
  EXPECT_EQ(0, tree.size());
  EXPECT_TRUE(tree.empty());
}

TEST(IntervalTreeTest, EraseRangePrefixLeavesSuffix) {
  TestIntervalTree tree;
  tree.insert(TestRange(2, 4));
  ASSERT_EQ(TestRange(2, 4), tree.find(2)->second);

  tree.erase(2);

  EXPECT_EQ(tree.end(), tree.find(1));
  EXPECT_EQ(tree.end(), tree.find(2));
  EXPECT_EQ(TestRange(3, 4), tree.find(3)->second);
  EXPECT_EQ(tree.end(), tree.find(4));
  EXPECT_EQ(1, tree.size());
}

TEST(IntervalTreeTest, EraseRangeSuffixLeavesPrefix) {
  TestIntervalTree tree;
  tree.insert(TestRange(2, 4));
  ASSERT_EQ(TestRange(2, 4), tree.find(2)->second);

  tree.erase(3);

  EXPECT_EQ(tree.end(), tree.find(1));
  EXPECT_EQ(TestRange(2, 3), tree.find(2)->second);
  EXPECT_EQ(tree.end(), tree.find(3));
  EXPECT_EQ(tree.end(), tree.find(4));
  EXPECT_EQ(1, tree.size());
}

TEST(IntervalTreeTest, EraseRangeMiddleLeavesPrefixAndSuffix) {
  TestIntervalTree tree;
  tree.insert(TestRange(2, 5));
  ASSERT_EQ(TestRange(2, 5), tree.find(2)->second);

  tree.erase(3);

  EXPECT_EQ(tree.end(), tree.find(1));
  EXPECT_EQ(TestRange(2, 3), tree.find(2)->second);
  EXPECT_EQ(tree.end(), tree.find(3));
  EXPECT_EQ(TestRange(4, 5), tree.find(4)->second);
  EXPECT_EQ(tree.end(), tree.find(5));
  EXPECT_EQ(2, tree.size());
}

TEST(IntervalTreeTest, EraseByRangeCanRemoveEntireRange) {
  TestIntervalTree tree;
  tree.insert(TestRange(2, 5));
  tree.erase(TestRange(2, 5));
  EXPECT_TRUE(tree.empty());
}

TEST(IntervalTreeTest, EraseByRangeCanRemovePrefix) {
  TestIntervalTree tree;
  tree.insert(TestRange(2, 5));
  tree.erase(TestRange(1, 3));

  EXPECT_EQ(1, tree.size());
  EXPECT_EQ(TestRange(3, 5), tree.begin()->second);
}

TEST(IntervalTreeTest, EraseByRangeCanRemoveSuffix) {
  TestIntervalTree tree;
  tree.insert(TestRange(2, 5));
  tree.erase(TestRange(4, 6));

  EXPECT_EQ(1, tree.size());
  EXPECT_EQ(TestRange(2, 4), tree.begin()->second);
}

TEST(IntervalTreeTest, EraseByRangeCanSplitRange) {
  TestIntervalTree tree;
  tree.insert(TestRange(2, 5));
  tree.erase(TestRange(3, 4));

  EXPECT_EQ(2, tree.size());
  auto iter = tree.begin();
  EXPECT_EQ(TestRange(2, 3), iter->second);
  iter++;
  EXPECT_EQ(TestRange(4, 5), iter->second);
}

TEST(IntervalTreeTest, EraseByRangeCanEraseMultipleRanges) {
  TestIntervalTree tree;
  tree.insert(TestRange(2, 3));
  tree.insert(TestRange(4, 5));
  tree.insert(TestRange(6, 7));
  ASSERT_EQ(3, tree.size());

  tree.erase(TestRange(2, 7));
  EXPECT_EQ(0, tree.size());
}

TEST(IntervalTreeTest, EraseByRangeCanEraseMultipleRangesAndLeaveEdges) {
  TestIntervalTree tree;
  tree.insert(TestRange(1, 3));
  tree.insert(TestRange(4, 5));
  tree.insert(TestRange(6, 8));
  ASSERT_EQ(3, tree.size());

  tree.erase(TestRange(2, 7));
  EXPECT_EQ(2, tree.size());
  auto iter = tree.begin();
  EXPECT_EQ(TestRange(1, 2), iter->second);
  iter++;
  EXPECT_EQ(TestRange(7, 8), iter->second);
}

TEST(IntervalTreeTest, FindRangeByNonOverlappingRangeReturnsEnd) {
  TestIntervalTree tree;
  tree.insert(TestRange(2, 5));
  tree.insert(TestRange(7, 10));
  ASSERT_EQ(tree.end(), tree.find(TestRange(5, 6)));
}

TEST(IntervalTreeTest, FindRangeByExactRange) {
  TestIntervalTree tree;
  tree.insert(TestRange(2, 5));
  ASSERT_EQ(TestRange(2, 5), tree.find(TestRange(2, 5))->second);
}

TEST(IntervalTreeTest, FindRangeByOverlappingPrefixRange) {
  TestIntervalTree tree;
  tree.insert(TestRange(2, 5));
  ASSERT_EQ(TestRange(2, 5), tree.find(TestRange(1, 3))->second);
}

TEST(IntervalTreeTest, FindRangeByOverlappingPrefixRangeAndAdjacentRange) {
  TestIntervalTree tree;
  tree.insert(TestRange(0, 1));
  tree.insert(TestRange(2, 5));
  ASSERT_EQ(TestRange(2, 5), tree.find(TestRange(1, 3))->second);
}

TEST(IntervalTreeTest, FindRangeByOverlappingSuffixRange) {
  TestIntervalTree tree;
  tree.insert(TestRange(2, 5));
  ASSERT_EQ(TestRange(2, 5), tree.find(TestRange(4, 6))->second);
}

TEST(IntervalTreeTest, FindRangeByOverlappingSuffixRangeAndAdjacentRange) {
  TestIntervalTree tree;
  tree.insert(TestRange(2, 5));
  tree.insert(TestRange(6, 7));
  ASSERT_EQ(TestRange(2, 5), tree.find(TestRange(4, 6))->second);
}

TEST(IntervalTreeTest, FindRangeOverlappingMultipleRangesReturnsFirst) {
  TestIntervalTree tree;
  tree.insert(TestRange(2, 5));
  tree.insert(TestRange(7, 8));
  tree.insert(TestRange(10, 15));
  ASSERT_EQ(TestRange(2, 5), tree.find(TestRange(0, 10))->second);
}

struct RangeContainer {
  RangeContainer(uint64_t start, uint64_t end, bool merge)
      : start_(start), end_(end), allow_merge_(merge) {}

  uint64_t start_ = 0;
  uint64_t end_ = 0;
  bool allow_merge_ = true;
};

struct RangeTraits {
  static uint64_t Start(const RangeContainer& obj) { return obj.start_; }
  static uint64_t End(const RangeContainer& obj) { return obj.end_; }

  static zx_status_t Update(const RangeContainer* other, uint64_t start, uint64_t end,
                            RangeContainer* obj) {
    if (other) {
      if (!obj->allow_merge_ || !other->allow_merge_) {
        return ZX_ERR_INTERNAL;
      }
    }
    obj->start_ = start;
    obj->end_ = end;
    return ZX_OK;
  }
};

using CustomRange = range::Range<uint64_t, RangeContainer, RangeTraits>;
using CustomTree = IntervalTree<CustomRange>;

TEST(IntervalTreeCustomMergeTest, RejectedInsertSameStartDoesNotModifyTree) {
  CustomTree tree;
  CustomRange range1(RangeContainer(5, 10, true));
  CustomRange range2(RangeContainer(5, 15, false));
  ASSERT_TRUE(tree.try_insert(range1));
  ASSERT_FALSE(tree.try_insert(range2));

  EXPECT_EQ(1, tree.size());
  EXPECT_EQ(range1, tree.begin()->second);
}

TEST(IntervalTreeCustomMergeTest, RejectedInsertOverlapPriorDoesNotModifyTree) {
  CustomTree tree;
  CustomRange range1(RangeContainer(5, 10, true));
  CustomRange range2(RangeContainer(3, 7, false));
  ASSERT_TRUE(tree.try_insert(range1));
  ASSERT_FALSE(tree.try_insert(range2));

  EXPECT_EQ(1, tree.size());
  EXPECT_EQ(range1, tree.begin()->second);
}

TEST(IntervalTreeCustomMergeTest, RejectedInsertOverlapNextDoesNotModifyTree) {
  CustomTree tree;
  CustomRange range1(RangeContainer(5, 10, true));
  CustomRange range2(RangeContainer(7, 12, false));
  ASSERT_TRUE(tree.try_insert(range1));
  ASSERT_FALSE(tree.try_insert(range2));

  EXPECT_EQ(1, tree.size());
  EXPECT_EQ(range1, tree.begin()->second);
}

TEST(IntervalTreeCustomMergeTest, RejectedInsertAdjacentPriorAddsRange) {
  CustomTree tree;
  CustomRange range1(RangeContainer(5, 10, true));
  CustomRange range2(RangeContainer(3, 5, false));

  ASSERT_TRUE(tree.try_insert(range1));
  ASSERT_TRUE(tree.try_insert(range2));

  EXPECT_EQ(2, tree.size());
  auto iter = tree.begin();
  EXPECT_EQ(range2, iter->second);
  iter++;
  EXPECT_EQ(range1, iter->second);
  iter++;
  EXPECT_EQ(tree.end(), iter);
}

TEST(IntervalTreeCustomMergeTest, RejectedInsertAdjacentNextAddsRange) {
  CustomTree tree;
  CustomRange range1(RangeContainer(5, 10, true));
  CustomRange range2(RangeContainer(10, 15, false));

  ASSERT_TRUE(tree.try_insert(range1));
  ASSERT_TRUE(tree.try_insert(range2));
  ASSERT_EQ(2, tree.size());

  auto iter = tree.begin();
  EXPECT_EQ(range1, iter++->second);
  EXPECT_EQ(range2, iter++->second);
  EXPECT_EQ(tree.end(), iter);
}

TEST(IntervalTreeCustomMergeTest, UnmergedAdjacentRangesAreIndexableByFind) {
  CustomTree tree;
  CustomRange range1(RangeContainer(1, 3, true));
  CustomRange range2(RangeContainer(3, 5, false));
  ASSERT_TRUE(tree.try_insert(range1));
  ASSERT_TRUE(tree.try_insert(range2));
  ASSERT_EQ(2, tree.size());

  EXPECT_EQ(tree.end(), tree.find(0));
  EXPECT_EQ(range1, tree.find(1)->second);
  EXPECT_EQ(range1, tree.find(2)->second);
  EXPECT_EQ(range2, tree.find(3)->second);
  EXPECT_EQ(range2, tree.find(4)->second);
  EXPECT_EQ(tree.end(), tree.find(5));
}

// [1, 3), [3, 5) + [0, 3) (merged) --> [0, 3), [3, 5)
TEST(IntervalTreeCustomMergeTest, UnmergedAdjacentRangesCanStillMergeWithPrior) {
  CustomTree tree;
  CustomRange range1(RangeContainer(1, 3, true));
  CustomRange range2(RangeContainer(3, 5, false));
  ASSERT_TRUE(tree.try_insert(range1));
  ASSERT_TRUE(tree.try_insert(range2));
  ASSERT_EQ(2, tree.size());

  CustomRange range3(RangeContainer(0, 3, true));
  ASSERT_TRUE(tree.try_insert(range3));
  EXPECT_EQ(2, tree.size());
  auto iter = tree.begin();
  EXPECT_EQ(range3, iter++->second);
  EXPECT_EQ(range2, iter++->second);
  EXPECT_EQ(tree.end(), iter);
}

// [1, 3), [3, 5) + [3, 6) (merged) --> [1, 3), [3, 6)
TEST(IntervalTreeCustomMergeTest, UnmergedAdjacentRangesCanStillMergeWithNext) {
  CustomTree tree;
  CustomRange range1(RangeContainer(1, 3, false));
  CustomRange range2(RangeContainer(3, 5, true));
  ASSERT_TRUE(tree.try_insert(range1));
  ASSERT_TRUE(tree.try_insert(range2));
  ASSERT_EQ(2, tree.size());

  CustomRange range3(RangeContainer(3, 6, true));
  ASSERT_TRUE(tree.try_insert(range3));
  EXPECT_EQ(2, tree.size());
  auto iter = tree.begin();
  EXPECT_EQ(range1, iter++->second);
  EXPECT_EQ(range3, iter++->second);
  EXPECT_EQ(tree.end(), iter);
}

// [1, 3), [3, 5) + [2, 4) (rejected) --> [1, 3), [3, 5)
TEST(IntervalTreeCustomMergeTest, UnmergedAdjacentRangesCannotMergeOverGap) {
  CustomTree tree;
  CustomRange range1(RangeContainer(1, 3, true));
  CustomRange range2(RangeContainer(3, 5, false));
  ASSERT_TRUE(tree.try_insert(range1));
  ASSERT_TRUE(tree.try_insert(range2));
  ASSERT_EQ(2, tree.size());

  CustomRange range3(RangeContainer(2, 4, true));
  ASSERT_FALSE(tree.try_insert(range3));
  EXPECT_EQ(2, tree.size());
  auto iter = tree.begin();
  EXPECT_EQ(range1, iter++->second);
  EXPECT_EQ(range2, iter++->second);
  EXPECT_EQ(tree.end(), iter);
}

TEST(IntervalTreeCustomMergeTest, UnmergeableInsertIsFatal) {
  CustomTree tree;
  CustomRange range1(RangeContainer(1, 3, true));
  CustomRange range2(RangeContainer(2, 5, false));

  ASSERT_TRUE(tree.try_insert(range1));
  ASSERT_FALSE(tree.try_insert(range2));

  // If "try_insert" returned false, then "insert" will be fatal.
  ASSERT_DEATH([&] { tree.insert(range2); });
}

}  // namespace
}  // namespace range

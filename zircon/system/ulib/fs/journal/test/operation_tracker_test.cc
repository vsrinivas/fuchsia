// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fs/journal/internal/operation_tracker.h>
#include <gtest/gtest.h>

namespace fs {
namespace {

using Range = range::Range<uint64_t>;
using OperationTracker = internal::OperationTracker;

TEST(OperationTracker, EmptyTrackerDoesNothing) { OperationTracker tracker; }

TEST(OperationTracker, RemoveFromEmptyTrackerDoesNothing) {
  OperationTracker tracker;
  std::vector<Range> overlap = tracker.Remove(Range(0, 10));
  EXPECT_EQ(overlap.size(), 0ul);
}

TEST(OperationTracker, InsertAndRemoveRange) {
  OperationTracker tracker;
  tracker.Insert(Range(0, 10));
  std::vector<Range> overlap = tracker.Remove(Range(0, 10));
  EXPECT_EQ(overlap.size(), 1ul);
  EXPECT_EQ(Range(0, 10), overlap[0]);
}

TEST(OperationTracker, InsertAndRemovePartialRange) {
  OperationTracker tracker;
  tracker.Insert(Range(0, 10));
  std::vector<Range> overlap = tracker.Remove(Range(0, 5));
  EXPECT_EQ(overlap.size(), 1ul);
  EXPECT_EQ(Range(0, 5), overlap[0]);
}

TEST(OperationTracker, RemoveRangePrefix) {
  OperationTracker tracker;
  tracker.Insert(Range(5, 10));
  std::vector<Range> overlap = tracker.Remove(Range(4, 6));
  EXPECT_EQ(overlap.size(), 1ul);
  EXPECT_EQ(Range(5, 6), overlap[0]);
}

TEST(OperationTracker, RemoveRangeSuffix) {
  OperationTracker tracker;
  tracker.Insert(Range(5, 10));
  std::vector<Range> overlap = tracker.Remove(Range(9, 10));
  EXPECT_EQ(overlap.size(), 1ul);
  EXPECT_EQ(Range(9, 10), overlap[0]);
}

TEST(OperationTracker, InsertAndRemoveSplitRange) {
  OperationTracker tracker;
  tracker.Insert(Range(0, 10));
  std::vector<Range> overlap = tracker.Remove(Range(3, 7));
  EXPECT_EQ(overlap.size(), 1ul);
  EXPECT_EQ(Range(3, 7), overlap[0]);
}

TEST(OperationTracker, RemoveFromMultipleRanges) {
  OperationTracker tracker;
  tracker.Insert(Range(0, 3));
  tracker.Insert(Range(7, 10));
  std::vector<Range> overlap = tracker.Remove(Range(2, 8));
  EXPECT_EQ(overlap.size(), 2ul);
  EXPECT_EQ(Range(2, 3), overlap[0]);
  EXPECT_EQ(Range(7, 8), overlap[1]);
}

}  // namespace
}  // namespace fs

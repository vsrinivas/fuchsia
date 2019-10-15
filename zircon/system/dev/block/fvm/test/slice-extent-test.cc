// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "slice-extent.h"

#include <memory>
#include <utility>

#include <zxtest/zxtest.h>

namespace fvm {
namespace {

// Verifies that given starting vslice, the extent describes an empty extent.
TEST(SliceExtentTest, CheckInitializationValues) {
  SliceExtent extent(1);
  EXPECT_EQ(extent.start(), 1);
  EXPECT_EQ(extent.end(), 1);
  EXPECT_EQ(extent.size(), 0);
  EXPECT_TRUE(extent.empty());
}

// Verify that added slices are retrieveable.
TEST(SliceExtentTest, AddSliceReflectsOk) {
  uint64_t pslice;
  SliceExtent extent(1);
  extent.push_back(/*pslice*/ 10);

  EXPECT_TRUE(extent.find(/*vslice*/ 1, &pslice));
  EXPECT_EQ(pslice, 10);
  EXPECT_EQ(extent.start(), 1);
  EXPECT_EQ(extent.end(), 2);
  EXPECT_EQ(extent.size(), 1);
}

// Verify that added slices are retrieveable.
TEST(SliceExtentTest, FindSliceNotPresent) {
  uint64_t pslice;
  SliceExtent extent(2);
  extent.push_back(/*pslice*/ 10);

  EXPECT_FALSE(extent.find(/*vslice*/ 1, &pslice));
  EXPECT_FALSE(extent.find(/*vslice*/ 3, &pslice));
}

// Verify that removing the single slice of an extent makes it empty.
TEST(SliceExtentTest, EmptyExtent) {
  SliceExtent extent(1);
  EXPECT_TRUE(extent.empty());
  extent.push_back(1);
  EXPECT_FALSE(extent.empty());
  extent.pop_back();
  EXPECT_TRUE(extent.empty());
}

// Verify that Split produces two disjoint extents at the specified vslice.
TEST(SliceExtentTest, SplitExtent) {
  SliceExtent extent(1);
  // vslice 1
  extent.push_back(2);
  // vslice 2
  extent.push_back(30);
  // vslice 3
  extent.push_back(14);
  // vslice 4
  extent.push_back(5);

  std::unique_ptr<SliceExtent> extent_2 = extent.Split(2);
  ASSERT_TRUE(extent_2);
  uint64_t pslice;

  EXPECT_EQ(extent.start(), 1);
  EXPECT_EQ(extent.end(), 3);
  ASSERT_TRUE(extent.find(1, &pslice));
  EXPECT_EQ(pslice, 2);
  ASSERT_TRUE(extent.find(2, &pslice));
  EXPECT_EQ(pslice, 30);

  EXPECT_EQ(extent_2->start(), 3);
  EXPECT_EQ(extent_2->end(), 5);
  ASSERT_TRUE(extent_2->find(3, &pslice));
  EXPECT_EQ(pslice, 14);
  ASSERT_TRUE(extent_2->find(4, &pslice));
  EXPECT_EQ(pslice, 5);
}

// Verify that Merge produces a correct extent.
TEST(SliceExtentTest, MergeExtent) {
  SliceExtent extent(1);
  SliceExtent extent_2(3);
  // vslice 1
  extent.push_back(2);
  // vslice 2
  extent.push_back(3);

  // vslice 3
  extent_2.push_back(4);
  // vslice 4
  extent_2.push_back(5);

  extent.Merge(std::move(extent_2));

  uint64_t pslice;

  EXPECT_EQ(extent.start(), 1);
  EXPECT_EQ(extent.end(), 5);
  ASSERT_TRUE(extent.find(1, &pslice));
  EXPECT_EQ(pslice, 2);
  ASSERT_TRUE(extent.find(2, &pslice));
  EXPECT_EQ(pslice, 3);
  ASSERT_TRUE(extent.find(3, &pslice));
  EXPECT_EQ(pslice, 4);
  ASSERT_TRUE(extent.find(4, &pslice));
  EXPECT_EQ(pslice, 5);
}

}  // namespace
}  // namespace fvm

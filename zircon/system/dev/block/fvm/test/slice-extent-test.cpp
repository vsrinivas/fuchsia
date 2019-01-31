// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "slice-extent.h"

#include <utility>

#include <fbl/unique_ptr.h>
#include <unittest/unittest.h>

namespace fvm {
namespace {

// Verifies that given starting vslice, the extent describes an empty extent.
bool InitializationValues() {
    BEGIN_TEST;
    SliceExtent extent(1);
    EXPECT_EQ(extent.start(), 1);
    EXPECT_EQ(extent.end(), 1);
    EXPECT_EQ(extent.size(), 0);
    EXPECT_TRUE(extent.is_empty());
    END_TEST;
}

// Verify that added slices are retrieveable.
bool AddSlice() {
    BEGIN_TEST;
    SliceExtent extent(1);
    // This would be our first virtual slice with offset 1.
    ASSERT_TRUE(extent.push_back(/*pslice*/ 10));
    EXPECT_EQ(extent.get(/*vslice*/ 1), 10);
    EXPECT_EQ(extent.start(), 1);
    EXPECT_EQ(extent.end(), 2);
    EXPECT_EQ(extent.size(), 1);
    EXPECT_EQ(extent.get(1), 10);
    END_TEST;
}

// Verify that removing the single slice of an extent makes it empty.
bool EmptyExtent() {
    BEGIN_TEST;
    SliceExtent extent(1);
    EXPECT_TRUE(extent.is_empty());
    extent.push_back(1);
    EXPECT_FALSE(extent.is_empty());
    extent.pop_back();
    EXPECT_TRUE(extent.is_empty());
    END_TEST;
}

// Verify that Split produces two disjoint extents at the specified vslice.
bool SplitExtent() {
    BEGIN_TEST;
    SliceExtent extent(1);
    // vslice 1
    extent.push_back(2);
    // vslice 2
    extent.push_back(30);
    // vslice 3
    extent.push_back(14);
    // vslice 4
    extent.push_back(5);

    fbl::unique_ptr<SliceExtent> extent_2 = extent.Split(2);
    ASSERT_TRUE(extent_2);

    EXPECT_EQ(extent.start(), 1);
    EXPECT_EQ(extent.end(), 3);
    EXPECT_EQ(extent.get(1), 2);
    EXPECT_EQ(extent.get(2), 30);

    EXPECT_EQ(extent_2->start(), 3);
    EXPECT_EQ(extent_2->end(), 5);
    EXPECT_EQ(extent_2->get(3), 14);
    EXPECT_EQ(extent_2->get(4), 5);

    END_TEST;
}

// Verify that Merge produces a correct extent.
bool MergeExtent() {
    BEGIN_TEST;
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

    ASSERT_TRUE(extent.Merge(std::move(extent_2)));

    EXPECT_EQ(extent.start(), 1);
    EXPECT_EQ(extent.end(), 5);
    EXPECT_EQ(extent.get(1), 2);
    EXPECT_EQ(extent.get(2), 3);
    EXPECT_EQ(extent.get(3), 4);
    EXPECT_EQ(extent.get(4), 5);

    END_TEST;
}

BEGIN_TEST_CASE(SliceExtentTest)
RUN_TEST(InitializationValues)
RUN_TEST(AddSlice)
RUN_TEST(EmptyExtent)
RUN_TEST(SplitExtent)
RUN_TEST(MergeExtent)
END_TEST_CASE(SliceExtentTest)
} // namespace
} // namespace fvm

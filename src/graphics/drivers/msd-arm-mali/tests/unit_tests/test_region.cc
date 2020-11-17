// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "region.h"

namespace {

TEST(Region, Union) {
  auto a = Region::FromStartAndEnd(0, 1);
  auto b = Region::FromStartAndEnd(2, 3);
  a.Union(b);
  EXPECT_EQ(a.start(), 0u);
  EXPECT_EQ(a.end(), 3u);

  a = Region();
  a.Union(b);
  EXPECT_EQ(a.start(), 2u);
  EXPECT_EQ(a.end(), 3u);

  b = Region();
  a = Region::FromStartAndEnd(0, 1);
  a.Union(b);
  EXPECT_EQ(a.start(), 0u);
  EXPECT_EQ(a.end(), 1u);

  b = Region();
  a = Region();
  a.Union(b);
  EXPECT_TRUE(a.empty());
}

TEST(Region, SubtractWithSplit) {
  {
    // Non-overlapping, b after a.
    Region a = Region::FromStartAndEnd(0, 1);
    Region b = Region::FromStartAndEnd(1, 3);
    auto [left, right] = a.SubtractWithSplit(b);
    EXPECT_EQ(left, a);
    EXPECT_TRUE(right.empty());
  }
  {
    // b overlaps tail of a.
    Region a = Region::FromStartAndEnd(0, 2);
    Region b = Region::FromStartAndEnd(1, 3);
    auto [left, right] = a.SubtractWithSplit(b);
    EXPECT_EQ(left, Region::FromStartAndEnd(0, 1));
    EXPECT_TRUE(right.empty());
  }
  {
    // b overlaps all of a
    Region a = Region::FromStartAndEnd(0, 2);
    Region b = Region::FromStartAndEnd(0, 3);
    auto [left, right] = a.SubtractWithSplit(b);
    EXPECT_TRUE(left.empty());
    EXPECT_TRUE(right.empty());
  }
  {
    // a is split by b.
    Region a = Region::FromStartAndEnd(0, 3);
    Region b = Region::FromStartAndEnd(1, 2);
    auto [left, right] = a.SubtractWithSplit(b);
    EXPECT_EQ(left, Region::FromStartAndEnd(0, 1));
    EXPECT_EQ(right, Region::FromStartAndEnd(2, 3));
  }
  {
    // b at start of a.
    Region a = Region::FromStartAndEnd(0, 2);
    Region b = Region::FromStartAndEnd(0, 1);
    auto [left, right] = a.SubtractWithSplit(b);
    EXPECT_EQ(left, Region::FromStartAndEnd(1, 2));
    EXPECT_TRUE(right.empty());
  }
  {
    // Non-overlapping, b before a.
    Region a = Region::FromStartAndEnd(1, 1);
    Region b = Region::FromStartAndEnd(0, 1);
    auto [left, right] = a.SubtractWithSplit(b);
    EXPECT_EQ(left, a);
    EXPECT_TRUE(right.empty());
  }
  {
    // b is empty
    Region a = Region::FromStartAndEnd(1, 2);
    Region b = Region::FromStartAndEnd(1, 1);
    auto [left, right] = a.SubtractWithSplit(b);
    EXPECT_EQ(left, a);
    EXPECT_TRUE(right.empty());
  }
  {
    // a is empty
    Region a = Region::FromStartAndEnd(1, 1);
    Region b = Region::FromStartAndEnd(5, 7);
    auto [left, right] = a.SubtractWithSplit(b);
    EXPECT_TRUE(left.empty());
    EXPECT_TRUE(right.empty());
  }
  {
    // both are empty
    Region a = Region::FromStartAndEnd(1, 1);
    Region b = Region::FromStartAndEnd(3, 3);
    auto [left, right] = a.SubtractWithSplit(b);
    EXPECT_TRUE(left.empty());
    EXPECT_TRUE(right.empty());
  }
}

}  // namespace

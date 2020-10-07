// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "union_find.h"

#include <gtest/gtest.h>

class UnionFindTest : public ::testing::Test {};

TEST_F(UnionFindTest, Singletons) {
  harvester::UnionFind<int> forest;

  forest.MakeSet(0);
  forest.MakeSet(1);
  forest.MakeSet(3);

  // All elements are singletons. Find() implicitly inserts 2 as a singleton.
  EXPECT_EQ(0, forest.Find(0));
  EXPECT_EQ(1, forest.Find(1));
  EXPECT_EQ(2, forest.Find(2));
  EXPECT_EQ(3, forest.Find(3));
}

TEST_F(UnionFindTest, Complex) {
  harvester::UnionFind<int> forest;

  for (int i = 0; i < 8; ++i) {
    forest.MakeSet(i);
  }

  // Make first set {0, 1, 2, 3}.
  forest.Union(0, 1);
  forest.Union(1, 2);
  forest.Union(3, 1);

  // Make second set {4, 5, 6, 7}.
  forest.Union(6, 7);
  forest.Union(4, 5);
  forest.Union(5, 7);

  // Expect that none of the elements in the two sets are InSameSet.
  for (int i = 0; i < 4; ++i) {
    for (int j = 4; j < 8; ++j) {
      EXPECT_FALSE(forest.InSameSet(i, j));
    }
  }

  // Check the first set that each of the elements is pair-wise InSameSet.
  for (int i = 0; i < 3; ++i) {
    for (int j = i + 1; j < 4; ++j) {
      EXPECT_TRUE(forest.InSameSet(i, j));
    }
  }

  // Join the two sets.
  forest.Union(1, 7);

  // After joining the sets, expect they're ALL InSameSet.
  for (int i = 0; i < 7; ++i) {
    for (int j = i + 1; j < 8; ++j) {
      EXPECT_TRUE(forest.InSameSet(i, j));
    }
  }
}

TEST_F(UnionFindTest, RepresentativeStability) {
  harvester::UnionFind<int> forest;

  // Make a set of {0..8}.
  for (int i = 1; i < 8; ++i) {
    forest.Union(i - 1, i);
  }

  // Check that the representative node is the same for all values in the set.
  int repr = forest.Find(0);
  for (int i = 1; i < 8; ++i) {
    EXPECT_TRUE(forest.InSameSet(repr, i));
  }
}

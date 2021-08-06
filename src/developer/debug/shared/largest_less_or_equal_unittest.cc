// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/largest_less_or_equal.h"

#include <functional>

#include <gtest/gtest.h>

namespace debug {

TEST(LargestLessOrEqual, Empty) {
  std::vector<int> empty;
  EXPECT_EQ(empty.end(), LargestLessOrEqual(empty.begin(), empty.end(), 25, std::less<int>(),
                                            std::equal_to<int>()));
}

TEST(LargestLessOrEqual, One) {
  std::vector<int> one{1};

  // Before begin.
  EXPECT_EQ(one.end(),
            LargestLessOrEqual(one.begin(), one.end(), 0, std::less<int>(), std::equal_to<int>()));

  // Equal.
  EXPECT_EQ(one.begin(),
            LargestLessOrEqual(one.begin(), one.end(), 1, std::less<int>(), std::equal_to<int>()));

  // Greater.
  EXPECT_EQ(one.begin(),
            LargestLessOrEqual(one.begin(), one.end(), 2, std::less<int>(), std::equal_to<int>()));
}

TEST(LargestLessOrEqual, Several) {
  std::vector<int> several{1, 3, 5};

  EXPECT_EQ(several.end(), LargestLessOrEqual(several.begin(), several.end(), 0, std::less<int>(),
                                              std::equal_to<int>()));
  EXPECT_EQ(several.begin(), LargestLessOrEqual(several.begin(), several.end(), 1, std::less<int>(),
                                                std::equal_to<int>()));
  EXPECT_EQ(several.begin(), LargestLessOrEqual(several.begin(), several.end(), 2, std::less<int>(),
                                                std::equal_to<int>()));
  EXPECT_EQ(several.begin() + 1, LargestLessOrEqual(several.begin(), several.end(), 3,
                                                    std::less<int>(), std::equal_to<int>()));
  EXPECT_EQ(several.begin() + 1, LargestLessOrEqual(several.begin(), several.end(), 4,
                                                    std::less<int>(), std::equal_to<int>()));
  EXPECT_EQ(several.begin() + 2, LargestLessOrEqual(several.begin(), several.end(), 5,
                                                    std::less<int>(), std::equal_to<int>()));
  EXPECT_EQ(several.begin() + 2, LargestLessOrEqual(several.begin(), several.end(), 6,
                                                    std::less<int>(), std::equal_to<int>()));
}

// Tests comparator usage when the contained item is not the same as the searched-for one.
TEST(LargestLessOrEqual, Container) {
  using Pair = std::pair<int, double>;
  std::vector<Pair> container{{1, 100.3}};

  auto pair_first_less = [](Pair p, int i) { return p.first < i; };
  auto pair_first_equal = [](Pair p, int i) { return p.first == i; };

  EXPECT_EQ(container.end(), LargestLessOrEqual(container.begin(), container.end(), 0,
                                                pair_first_less, pair_first_equal));
  EXPECT_EQ(container.begin(), LargestLessOrEqual(container.begin(), container.end(), 1,
                                                  pair_first_less, pair_first_equal));
  EXPECT_EQ(container.begin(), LargestLessOrEqual(container.begin(), container.end(), 2,
                                                  pair_first_less, pair_first_equal));
}

TEST(LargestLessOrEqual, Duplicates) {
  // Indices:                0  1  2  3  4  5  6
  std::vector<int> container{1, 1, 3, 3, 3, 5, 5};

  // Querying exactly for a duplicate will return the first one of that sequence.
  EXPECT_EQ(container.begin(), LargestLessOrEqual(container.begin(), container.end(), 1,
                                                  std::less<int>(), std::equal_to<int>()));
  EXPECT_EQ(container.begin() + 2, LargestLessOrEqual(container.begin(), container.end(), 3,
                                                      std::less<int>(), std::equal_to<int>()));
  EXPECT_EQ(container.begin() + 5, LargestLessOrEqual(container.begin(), container.end(), 5,
                                                      std::less<int>(), std::equal_to<int>()));

  // Querying for the following value will return the last one of the sequence.
  EXPECT_EQ(container.begin() + 1, LargestLessOrEqual(container.begin(), container.end(), 2,
                                                      std::less<int>(), std::equal_to<int>()));
  EXPECT_EQ(container.begin() + 4, LargestLessOrEqual(container.begin(), container.end(), 4,
                                                      std::less<int>(), std::equal_to<int>()));
  EXPECT_EQ(container.begin() + 6, LargestLessOrEqual(container.begin(), container.end(), 6,
                                                      std::less<int>(), std::equal_to<int>()));
}

}  // namespace debug

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/containers/cpp/array_view.h"

#include <gtest/gtest.h>

namespace containers {

TEST(ArrayView, Basic) {
  const int kValues[6] = {1, 2, 3, 4, 5, 6};

  array_view<int> default_constructed;
  EXPECT_EQ(0u, default_constructed.size());
  EXPECT_TRUE(default_constructed.empty());

  array_view<int> iter_constucted_empty(std::begin(kValues), std::begin(kValues));
  EXPECT_EQ(0u, iter_constucted_empty.size());
  EXPECT_TRUE(iter_constucted_empty.empty());

  array_view<int> iter_constucted_6(std::begin(kValues), std::end(kValues));
  EXPECT_EQ(6u, iter_constucted_6.size());
  EXPECT_FALSE(iter_constucted_6.empty());
  EXPECT_EQ(1, iter_constucted_6[0]);
  EXPECT_EQ(2, iter_constucted_6[1]);
  EXPECT_EQ(3, iter_constucted_6[2]);
  EXPECT_EQ(4, iter_constucted_6[3]);
  EXPECT_EQ(5, iter_constucted_6[4]);
  EXPECT_EQ(6, iter_constucted_6[5]);

  EXPECT_EQ(1, iter_constucted_6.front());
  EXPECT_EQ(6, iter_constucted_6.back());

  array_view<int> size_constructed(kValues, std::size(kValues));
  EXPECT_EQ(6u, iter_constucted_6.size());
  EXPECT_FALSE(iter_constucted_6.empty());
  EXPECT_EQ(1, iter_constucted_6[0]);
  EXPECT_EQ(6, iter_constucted_6[5]);

  std::vector<int> empty_vect;
  array_view<int> empty(empty_vect);
  EXPECT_TRUE(empty.empty());

  std::vector<int> nonempty_vect{1, 2};
  array_view<int> nonempty(nonempty_vect);
  EXPECT_EQ(2u, nonempty.size());
  EXPECT_FALSE(nonempty.empty());
  EXPECT_EQ(1, nonempty[0]);
  EXPECT_EQ(2, nonempty[1]);
}

TEST(ArrayView, Iterators) {
  const int kValues[4] = {1, 2, 3, 4};
  array_view<int> view(std::begin(kValues), std::end(kValues));

  // Range-based for loop.
  int expected_value = 1;
  for (auto i : view) {
    EXPECT_EQ(expected_value, i);
    expected_value++;
  }
  EXPECT_EQ(5, expected_value);

  // Reverse iterators.
  expected_value = 4;
  for (auto iter = view.rbegin(); iter != view.rend(); ++iter) {
    EXPECT_EQ(expected_value, *iter);
    expected_value--;
  }
  EXPECT_EQ(0, expected_value);
}

TEST(ArrayView, SubView) {
  const int kValues[5] = {1, 2, 3, 4, 5};
  array_view<int> source(std::begin(kValues), std::end(kValues));

  // Both arguments implicit.
  array_view<int> full_sub = source.subview();
  EXPECT_EQ(5u, full_sub.size());
  EXPECT_EQ(1, full_sub[0]);
  EXPECT_EQ(5, full_sub[4]);

  // Implicit end.
  array_view<int> implicit_sub = source.subview(2);
  EXPECT_EQ(3u, implicit_sub.size());
  EXPECT_EQ(3, implicit_sub[0]);
  EXPECT_EQ(5, implicit_sub[2]);

  // Explicit end.
  array_view<int> explicit_sub = source.subview(1, 3);
  EXPECT_EQ(3u, explicit_sub.size());
  EXPECT_EQ(2, explicit_sub[0]);
  EXPECT_EQ(4, explicit_sub[2]);

  // End matching real end.
  array_view<int> matching_sub = source.subview(2, 3);
  EXPECT_EQ(3u, matching_sub.size());
  EXPECT_EQ(3, matching_sub[0]);
  EXPECT_EQ(5, matching_sub[2]);

  // Size past the end.
  array_view<int> overflow_sub = source.subview(3, 9);
  EXPECT_EQ(2u, overflow_sub.size());
  EXPECT_EQ(4, overflow_sub[0]);
  EXPECT_EQ(5, overflow_sub[1]);

  // Source equals the end.
  array_view<int> source_equals_sub = source.subview(5, 9);
  EXPECT_EQ(true, source_equals_sub.empty());

  // Source equals the end.
  array_view<int> source_past_sub = source.subview(9, 2);
  EXPECT_EQ(true, source_past_sub.empty());
}

}  // namespace containers

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/vector.h>

#include "gtest.h"

namespace {

TEST(VectorTest, EraseRemovesAllMatchingEntries) {
  std::vector<int> v = {1, 2, 1, 2, 1, 3, 4, 5, 1};

  ASSERT_EQ(cpp20::erase(v, 1), 4u);

  EXPECT_THAT(v, testing::ElementsAreArray({2, 2, 3, 4, 5}));
}

TEST(VectorTest, EraseWithNoEntries) {
  std::vector<int> v = {1, 2, 1, 2, 1, 3, 4, 5};

  ASSERT_EQ(cpp20::erase(v, 6), 0u);

  EXPECT_THAT(v, testing::ElementsAreArray({1, 2, 1, 2, 1, 3, 4, 5}));
}

TEST(VectorTest, EraseIfRemovesAllMatchingEntries) {
  std::vector<int> v = {1, 2, 1, 2, 1, 3, 4, 5, 1};

  ASSERT_EQ(cpp20::erase_if(v, [](auto b) { return b == 1 || b == 2; }), 6u);

  EXPECT_THAT(v, testing::ElementsAreArray({3, 4, 5}));
}

TEST(VectorTest, EraseIfWithNoEntries) {
  std::vector<int> v = {1, 2, 1, 2, 1, 3, 4, 5};

  // Nothing matches.
  ASSERT_EQ(cpp20::erase_if(v, [](auto b) { return false; }), 0u);

  EXPECT_THAT(v, testing::ElementsAreArray({1, 2, 1, 2, 1, 3, 4, 5}));
}

#if defined(__cpp_lib_erase_if) && __cpp_lib_erase_if >= 202002 && \
    !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(VectorTest, EraseVariantsAreAliasForStdWhenAvailable) {
  using size_type = std::vector<int>::size_type;
  using arg = std::vector<int>;
  using value = int;

  constexpr size_type (*cpp20_erase)(arg&, const value&) =
      &cpp20::erase<int, std::vector<int>::allocator_type, int>;
  constexpr size_type (*std_erase)(arg&, const value&) =
      &std::erase<int, std::vector<int>::allocator_type, int>;
  static_assert(cpp20_erase == std_erase, "");

  using pred = bool (*)(value);
  constexpr size_type (*cpp20_erase_if)(arg&, pred) =
      &cpp20::erase_if<int, std::vector<int>::allocator_type, pred>;
  constexpr size_type (*std_erase_if)(arg&, pred) =
      &std::erase_if<int, std::vector<int>::allocator_type, pred>;
  static_assert(cpp20_erase_if == std_erase_if, "");
}

#endif

}  // namespace

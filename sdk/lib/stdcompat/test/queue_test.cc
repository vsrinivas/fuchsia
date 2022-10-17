// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/queue.h>

#include "gtest.h"

namespace {

TEST(DequeTest, EraseRemovesAllMatchingEntries) {
  std::deque<int> v = {1, 2, 1, 2, 1, 3, 4, 5, 1};

  ASSERT_EQ(cpp20::erase(v, 1), 4u);

  EXPECT_THAT(v, testing::ElementsAreArray({2, 2, 3, 4, 5}));
}

TEST(DequeTest, EraseWithNoEntries) {
  std::deque<int> v = {1, 2, 1, 2, 1, 3, 4, 5};

  ASSERT_EQ(cpp20::erase(v, 6), 0u);

  EXPECT_THAT(v, testing::ElementsAreArray({1, 2, 1, 2, 1, 3, 4, 5}));
}

TEST(DequeTest, EraseIfRemovesAllMatchingEntries) {
  std::deque<int> v = {1, 2, 1, 2, 1, 3, 4, 5, 1};

  ASSERT_EQ(cpp20::erase_if(v, [](auto b) { return b == 1 || b == 2; }), 6u);

  EXPECT_THAT(v, testing::ElementsAreArray({3, 4, 5}));
}

TEST(DequeTest, EraseIfWithNoEntries) {
  std::deque<int> v = {1, 2, 1, 2, 1, 3, 4, 5};

  // Nothing matches.
  ASSERT_EQ(cpp20::erase_if(v, [](auto b) { return false; }), 0u);

  EXPECT_THAT(v, testing::ElementsAreArray({1, 2, 1, 2, 1, 3, 4, 5}));
}

TEST(DequeTest, EraseWithStrings) {
  std::deque<std::string> container = {"value 1", "value 2"};
  cpp20::erase(container, "value 2");

  ASSERT_EQ(container.size(), 1u);
  ASSERT_EQ(container.front(), "value 1");
}

#if defined(__cpp_lib_erase_if) && __cpp_lib_erase_if >= 202002 && \
    !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(DequeTest, EraseVariantsAreAliasForStdWhenAvailable) {
  using size_type = std::deque<int>::size_type;
  using arg = std::deque<int>;
  using value = int;

  constexpr size_type (*cpp20_erase)(arg&, const value&) =
      &cpp20::erase<int, std::deque<int>::allocator_type, int>;
  constexpr size_type (*std_erase)(arg&, const value&) =
      &std::erase<int, std::deque<int>::allocator_type, int>;
  static_assert(cpp20_erase == std_erase, "");

  using pred = bool (*)(value);
  constexpr size_type (*cpp20_erase_if)(arg&, pred) =
      &cpp20::erase_if<int, std::deque<int>::allocator_type, pred>;
  constexpr size_type (*std_erase_if)(arg&, pred) =
      &std::erase_if<int, std::deque<int>::allocator_type, pred>;
  static_assert(cpp20_erase_if == std_erase_if, "");
}

#endif

}  // namespace

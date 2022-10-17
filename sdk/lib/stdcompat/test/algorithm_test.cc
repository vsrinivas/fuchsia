// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/stdcompat/internal/algorithm.h"

#include <lib/stdcompat/algorithm.h>
#include <lib/stdcompat/array.h>
#include <lib/stdcompat/span.h>
#include <lib/stdcompat/string_view.h>

#include <array>
#include <cstdint>
#include <deque>
#include <iostream>
#include <queue>
#include <vector>

#include "gtest.h"

namespace {

template <typename T, typename Comp>
constexpr bool IsHeap(const T& container, Comp& cmp) {
  if (container.empty()) {
    return true;
  }
  for (size_t i = 1; i < container.size() - 1; ++i) {
    const auto& parent = container[i / 2];
    if (cmp(container[i], parent)) {
      return false;
    }
  }
  return true;
}

template <typename T, typename Comp>
constexpr bool IsSorted(const T& container, Comp& cmp) {
  if (container.empty()) {
    return true;
  }

  for (size_t i = 0; i < container.size() - 1; ++i) {
    if (!cmp(container[i], container[i + 1])) {
      return false;
    }
  }
  return true;
}
constexpr auto kLessThan = [](const auto& a, const auto& b) { return a < b; };
constexpr auto kGreaterThan = [](const auto& a, const auto& b) { return a > b; };
constexpr auto kLessOrEqual = [](const auto& a, const auto& b) { return a <= b; };
constexpr auto kGreaterOrEqual = [](const auto& a, const auto& b) { return a >= b; };

TEST(InternalMakeHeapTest, GeneratesValidHeap) {
  constexpr auto make_heap = [](auto container, auto cmp) {
    cpp20::internal::make_heap(container.begin(), container.end(), cmp);
    return container;
  };
  static_assert(IsHeap(make_heap(cpp20::to_array({1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 5}), kLessThan),
                       kLessThan),
                "");

  static_assert(IsHeap(make_heap(cpp20::span<uint8_t>(), kLessThan), kLessThan), "");
  static_assert(IsHeap(make_heap(cpp20::to_array({2, 3, 1}), kLessThan), kLessThan), "");
  static_assert(IsHeap(make_heap(cpp20::to_array({2, 3, 4, 1}), kLessThan), kLessThan), "");
  static_assert(IsHeap(make_heap(cpp20::to_array({2, 3, 6, 5, 1}), kLessThan), kLessThan), "");
  static_assert(IsHeap(make_heap(cpp20::to_array({2, 3, 6, 5, 6, 1}), kLessThan), kLessThan), "");
}

TEST(InternalSortHeapTest, SortedHeapOrderIsReverseToComparison) {
  constexpr auto make_sorted_heap = [](auto container, auto cmp) {
    cpp20::internal::make_heap(container.begin(), container.end(), cmp);
    cpp20::internal::sort_heap(container.begin(), container.end(), cmp);
    return container;
  };

  static_assert(
      IsSorted(make_sorted_heap(cpp20::to_array({1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 5}), kLessThan),
               kGreaterOrEqual),
      "");

  static_assert(IsSorted(make_sorted_heap(cpp20::to_array({1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 0, 5}),
                                          kGreaterThan),
                         kLessOrEqual),
                "");

  static_assert(IsSorted(make_sorted_heap(cpp20::to_array<cpp17::string_view>(
                                              {"1", "Foo", "2", "345", "1", "678", "0"}),
                                          kLessThan),
                         kGreaterOrEqual),
                "");

  static_assert(IsSorted(make_sorted_heap(cpp20::to_array<cpp17::string_view>(
                                              {"1", "Foo", "2", "345", "1", "678", "0"}),
                                          kGreaterThan),
                         kLessOrEqual),
                "");
}

TEST(SortTest, ElementsAreSorted) {
  constexpr auto sort_container = [](auto container, auto& comp) {
    cpp20::sort(container.begin(), container.end(), comp);
    return container;
  };
  static_assert(
      IsSorted(sort_container(cpp20::to_array({0, 2, 4, 1, 3, 4, 5, 6}), kLessThan), kLessOrEqual),
      "");

  static_assert(IsSorted(sort_container(cpp20::to_array({0, 2, 4, 1, 3, 4, 5, 6}), kGreaterThan),
                         kGreaterOrEqual),
                "");
}

TEST(IsSortedTest, ElemenstAreSorted) {
  constexpr auto kDataAscending = cpp20::to_array({1, 2, 3, 4, -1});

  // Subrange is sorted.
  static_assert(cpp20::is_sorted(kDataAscending.begin(), kDataAscending.end() - 1), "");
  ASSERT_TRUE(std::is_sorted(kDataAscending.begin(), kDataAscending.end() - 1));

  // Full array is not.
  static_assert(!cpp20::is_sorted(kDataAscending.begin(), kDataAscending.end()), "");
  ASSERT_FALSE(std::is_sorted(kDataAscending.begin(), kDataAscending.end()));

  // Same but in reverse order with custom comparison.
  constexpr auto kDataDescending = cpp20::to_array({4, 3, 2, 1, 5});

  // Subrange is sorted.
  static_assert(
      cpp20::is_sorted(kDataDescending.begin(), kDataDescending.end() - 1, std::greater{}), "");
  ASSERT_TRUE(std::is_sorted(kDataDescending.begin(), kDataDescending.end() - 1, std::greater{}));

  // Full array is not.
  static_assert(!cpp20::is_sorted(kDataDescending.begin(), kDataDescending.end(), std::greater{}),
                "");
  ASSERT_FALSE(std::is_sorted(kDataDescending.begin(), kDataDescending.end(), std::greater{}));

  // Same but in reverse order with custom comparison.
  constexpr auto kDataWithRepeatedElements = cpp20::to_array({4, 3, 2, 2, 1, 5});
  // Full array is not.
  static_assert(cpp20::is_sorted(kDataWithRepeatedElements.begin(),
                                 kDataWithRepeatedElements.end() - 1, std::greater{}),
                "");
  ASSERT_TRUE(std::is_sorted(kDataWithRepeatedElements.begin(), kDataWithRepeatedElements.end() - 1,
                             std::greater{}));
}

TEST(RemoveIfTest, ElementsAreRemoved) {
  std::vector<int> container = {1, 2, 3, 4, 5, 1, 2, 3, 4, 5};
  std::vector<int> expected = {2, 3, 4, 5, 2, 3, 4, 5};
  size_t expected_size = container.size();

  auto new_end =
      cpp20::remove_if(container.begin(), container.end(), [](const auto& a) { return a == 1; });

  ASSERT_EQ(container.size(), expected_size);
  ASSERT_THAT(expected, testing::UnorderedElementsAreArray(container.begin(), new_end));
}

TEST(RemoveTest, ElementsAreRemoved) {
  std::vector<int> container = {1, 2, 3, 4, 5, 1, 2, 3, 4, 5};
  std::vector<int> expected = {2, 3, 4, 5, 2, 3, 4, 5};
  size_t expected_size = container.size();

  auto new_end = cpp20::remove(container.begin(), container.end(), 1);

  ASSERT_EQ(container.size(), expected_size);
  ASSERT_THAT(expected, testing::UnorderedElementsAreArray(container.begin(), new_end));
}

TEST(RemoveTest, IsAliasWhenStdIsAvailable) {
  auto check = [](auto&& container) {
    constexpr auto predicate = [](auto& a) { return true; };
    using pred = decltype(predicate);
    using it = decltype(std::declval<decltype(container)>().begin());
    constexpr it (*cpp20_remove_if)(it, it, pred) = cpp20::remove_if;
    constexpr it (*std_remove_if)(it, it, pred) = cpp20::remove_if;

    static_assert(cpp20_remove_if == std_remove_if, "");
  };

  check(std::vector<int>());
  check(std::array<int, 4>());
}

TEST(RemoveTest, WithSelfMoveAvoided) {
  std::deque<std::string> container = {"value 1", "value 2"};
  cpp20::remove(container.begin(), container.end(), "value 2");

  ASSERT_EQ(container.size(), 2u);
  ASSERT_EQ(container.front(), "value 1");
}

#if defined(__cpp_lib_constexpr_algorithms) && __cpp_lib_constexpr_algorithms >= 201806L && \
    !defined(LIB_STDCOMPAT_USE_POLYFILLS)

TEST(SortTest, IsAliasWhenStdIsAvailable) {
  {
    using iterator_type = decltype(std::declval<std::vector<int>>().begin());
    constexpr void (*sort_cpp20)(iterator_type, iterator_type) = cpp20::sort;
    constexpr void (*sort_std)(iterator_type, iterator_type) = std::sort;

    static_assert(sort_cpp20 == sort_std, "cpp20::sort should be an alias of std::sort.");
  }

  {
    using iterator_type = decltype(std::declval<std::vector<int>>().begin());
    auto comp = [](const int& a, const int& b) -> bool { return false; };
    constexpr void (*sort_cpp20)(iterator_type, iterator_type, decltype(comp)) = cpp20::sort;
    constexpr void (*sort_std)(iterator_type, iterator_type, decltype(comp)) = std::sort;

    static_assert(sort_cpp20 == sort_std, "cpp20::sort should be an alias of std::sort.");
  }
}

TEST(IsSortedTest, IsAliasWhenStdIsAvailable) {
  {
    using iterator_type = decltype(std::declval<std::vector<int>>().begin());
    constexpr bool (*is_sorted_cpp20)(iterator_type, iterator_type) = cpp20::is_sorted;
    constexpr bool (*is_sorted_std)(iterator_type, iterator_type) = std::is_sorted;

    static_assert(is_sorted_cpp20 == is_sorted_std,
                  "cpp20::is_sorted_std should be an alias of std::sort.");
  }

  {
    using iterator_type = decltype(std::declval<std::vector<int>>().begin());
    auto comp = [](const int& a, const int& b) -> bool { return false; };
    constexpr bool (*is_sorted_cpp20)(iterator_type, iterator_type, decltype(comp)) =
        cpp20::is_sorted;
    constexpr bool (*is_sorted_std)(iterator_type, iterator_type, decltype(comp)) = std::is_sorted;

    static_assert(is_sorted_cpp20 == is_sorted_std,
                  "cpp20::is_sorted_std should be an alias of std::sort.");
  }
}

#endif  // __cpp_lib_constexpr_algorithms >= 201806L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

}  // namespace

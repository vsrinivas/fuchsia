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
#include <iostream>
#include <vector>

#include <gtest/gtest.h>

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

#if __cpp_lib_constexpr_algorithms >= 201806L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

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

#endif  // __cpp_lib_constexpr_algorithms >= 201806L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

}  // namespace

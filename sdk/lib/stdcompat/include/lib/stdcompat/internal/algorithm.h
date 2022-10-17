// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_ALGORITHM_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_ALGORITHM_H_

#include <cstdint>
#include <iterator>
#include <type_traits>

#include "../utility.h"

namespace cpp20 {
namespace internal {

// Given a range [first, end) non inclusive, creates a valid heap where comparison(parent, child) is
// valid for all nodes.
template <typename RandomIterator, typename Comparator>
constexpr void make_heap(RandomIterator first, RandomIterator end, Comparator comparison) {
  constexpr auto insert_element = [](RandomIterator root, size_t element_index,
                                     Comparator& comparison) {
    while (element_index > 0) {
      size_t parent_index = element_index / 2;
      auto& parent = *(root + parent_index);
      auto& element = *(root + element_index);
      if (comparison(parent, element)) {
        break;
      }
      element = cpp20::exchange(parent, std::move(element));
      element_index = parent_index;
    }
  };

  const size_t length = std::distance(first, end);
  for (size_t curr = 1; curr < length; ++curr) {
    insert_element(first, curr, comparison);
  }
}

// Given a heap whose nodes satisfy comparison(parent, child) for every node, returns a sorted array
// whose element i satisfies comparison(j, i) for every j > i.
template <typename RandomIterator, typename Comparator>
constexpr void sort_heap(RandomIterator first, RandomIterator end, Comparator comparison) {
  // Length is always positive.
  constexpr auto extract_element = [](RandomIterator root, size_t length, Comparator& comparison) {
    auto& element = *(root + length - 1);
    element = cpp20::exchange(*root, std::move(element));
    // The last item has been extracted from the heap, so length is reduced.
    size_t element_index = 0;
    length--;
    while (element_index < length) {
      const size_t left = element_index * 2;
      const size_t right = element_index * 2 + 1;
      size_t parent_candidate = element_index;
      if (left < length && comparison(*(root + left), *(root + parent_candidate))) {
        parent_candidate = left;
      }

      if (right < length && comparison(*(root + right), *(root + parent_candidate))) {
        parent_candidate = right;
      }

      if (element_index == parent_candidate) {
        break;
      }
      auto& parent_cand_element = *(root + parent_candidate);
      parent_cand_element =
          cpp20::exchange(*(root + element_index), std::move(parent_cand_element));
      element_index = parent_candidate;
    }
  };

  // Each extract is O(log n), which yields O(n log n).
  const size_t length = std::distance(first, end);
  for (size_t curr = 0; curr < length; ++curr) {
    extract_element(first, length - curr, comparison);
  }
}

// The sorting algorithm implemented here is heapsort.
template <typename RandomIterator, typename Comparator>
constexpr void sort(RandomIterator first, RandomIterator end, Comparator comparison) {
  static_assert(
      std::is_base_of<std::random_access_iterator_tag,
                      typename std::iterator_traits<RandomIterator>::iterator_category>::value,
      "cpp20::sort requires random access iterators.");
  // Reverse the comparison to generate a heap with the opposite parent-child comparison.
  const auto reverse_comp = [&comparison](const auto& a, const auto& b) {
    return !comparison(a, b);
  };
  make_heap(first, end, reverse_comp);
  sort_heap(first, end, reverse_comp);
}

template <typename ForwardIt, typename Comparator>
constexpr bool is_sorted(ForwardIt start, ForwardIt end, Comparator comp) {
  static_assert(std::is_base_of<std::forward_iterator_tag,
                                typename std::iterator_traits<ForwardIt>::iterator_category>::value,
                "cpp20::is_sorted requires forward iterators.");
  auto curr = start;
  auto next = ++start;
  // If curr >= end then next > end.
  // We treat a range [a, b], where a > b, as an empty range, since no elements are within such
  // range.
  while (next < end) {
    if (comp(*next, *curr)) {
      return false;
    }
    curr = next;
    next++;
  }
  return true;
}

template <typename ForwardIt, typename Pred>
constexpr ForwardIt remove_if(ForwardIt begin, ForwardIt end, Pred pred) {
  auto remaining_end = begin;
  for (auto it = begin; it != end; ++it) {
    auto& value = *it;
    if (!pred(cpp17::as_const(value))) {
      // Avoid self move.
      if (remaining_end != it) {
        *remaining_end = std::move(value);
      }
      ++remaining_end;
    }
  }
  return remaining_end;
}

template <typename ForwardIt, typename T>
constexpr ForwardIt remove(ForwardIt begin, ForwardIt end, const T& val) {
  return cpp20::internal::remove_if(begin, end, [&val](const auto& a) { return a == val; });
}

}  // namespace internal
}  // namespace cpp20

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_INTERNAL_ALGORITHM_H_

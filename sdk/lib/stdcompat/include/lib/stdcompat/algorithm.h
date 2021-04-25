// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_ALGORITHM_H_
#define LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_ALGORITHM_H_

#include <algorithm>

#include "internal/algorithm.h"

namespace cpp20 {

#if __cpp_lib_constexpr_algorithms >= 201806L && !defined(LIB_STDCOMPAT_USE_POLYFILLS)

using std::is_sorted;
using std::sort;

#else

template <typename RandomIterator, typename Comparator = std::less<>>
constexpr void sort(RandomIterator first, RandomIterator end, Comparator comp = Comparator{}) {

#if LIB_STDCOMPAT_CONSTEVAL_SUPPORT

  if (!cpp20::is_constant_evaluated()) {
    return std::sort(first, end, comp);
  }

#endif  // LIB_STDCOMPAT_CONSTEVAL_SUPPORT
  return cpp20::internal::sort(first, end, comp);
}

template <typename ForwardIt, typename Comparator = std::less<>>
constexpr bool is_sorted(ForwardIt first, ForwardIt end, Comparator comp = Comparator{}) {

#if LIB_STDCOMPAT_CONSTEVAL_SUPPORT

  if (!cpp20::is_constant_evaluated()) {
    return std::is_sorted(first, end, comp);
  }

#endif  // LIB_STDCOMPAT_CONSTEVAL_SUPPORT
  return cpp20::internal::is_sorted(first, end, comp);
}

#endif

}  // namespace cpp20

#endif  // LIB_STDCOMPAT_INCLUDE_LIB_STDCOMPAT_ALGORITHM_H_

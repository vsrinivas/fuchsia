// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_ALGORITHM_H_
#define FBL_ALGORITHM_H_

#include <stdlib.h>

#include <algorithm>
#include <type_traits>

namespace fbl {

using std::clamp;
using std::max;
using std::min;

// is_pow2
//
// Test to see if an unsigned integer type is an exact power of two or
// not.  Note, this needs to use a helper struct because we are not
// allowed to partially specialize functions (because C++).
namespace internal {
template <typename T, typename Enable = void>
struct IsPow2Helper;

template <typename T>
struct IsPow2Helper<T, std::enable_if_t<std::is_unsigned_v<T>>> {
  static constexpr bool is_pow2(T val) { return (val != 0) && (((val - 1U) & val) == 0); }
};
}  // namespace internal

// is_pow2<T>(T val)
//
// Tests to see if val (which may be any unsigned integer type) is a power of 2
// or not.  0 is not considered to be a power of 2.
//
template <typename T>
constexpr bool is_pow2(T val) {
  return internal::IsPow2Helper<T>::is_pow2(val);
}

// round_up rounds up val until it is divisible by multiple.
// Zero is divisible by all multiples.
template <class T, class U, class L = std::conditional_t<sizeof(T) >= sizeof(U), T, U>,
          class = std::enable_if_t<std::is_unsigned_v<T>>,
          class = std::enable_if_t<std::is_unsigned_v<U>>>
constexpr const L round_up(const T& val_, const U& multiple_) {
  const L val = static_cast<L>(val_);
  const L multiple = static_cast<L>(multiple_);
  return val == 0 ? 0
                  : is_pow2<L>(multiple) ? (val + (multiple - 1)) & ~(multiple - 1)
                                         : ((val + (multiple - 1)) / multiple) * multiple;
}

// round_down rounds down val until it is divisible by multiple.
// Zero is divisible by all multiples.
template <class T, class U, class L = std::conditional_t<sizeof(T) >= sizeof(U), T, U>,
          class = std::enable_if_t<std::is_unsigned_v<T>>,
          class = std::enable_if_t<std::is_unsigned_v<U>>>
constexpr const L round_down(const T& val_, const U& multiple_) {
  const L val = static_cast<L>(val_);
  const L multiple = static_cast<L>(multiple_);
  return val == 0 ? 0 : is_pow2<L>(multiple) ? val & ~(multiple - 1) : (val / multiple) * multiple;
}

// Returns an iterator to the maximum element in the range [|first|, |last|).
//
// |first| and |last| must be forward iterators.
//
// Similar to: <http://en.cppreference.com/w/cpp/algorithm/max_element>
template <class FwIterator>
FwIterator max_element(FwIterator first, FwIterator last) {
  FwIterator max = first;
  while (first < last) {
    if (*first > *max) {
      max = first;
    }
    first++;
  }
  return max;
}

// Returns an iterator to the maximum element in the range [|first|, |last|).
// using |comp| to compare elements instead of operator>
//
// |first| and |last| must be forward iterators.
//
// Similar to: <http://en.cppreference.com/w/cpp/algorithm/max_element>
template <class FwIterator, class Compare>
FwIterator max_element(FwIterator first, FwIterator last, Compare comp) {
  FwIterator max = first;
  while (first < last) {
    if (comp(*first, *max)) {
      max = first;
    }
    first++;
  }
  return max;
}

// Returns an iterator to the minimum element in the range [|first|, |last|).
//
// |first| and |last| must be forward iterators.
//
// Similar to: <http://en.cppreference.com/w/cpp/algorithm/min_element>
template <class FwIterator>
FwIterator min_element(FwIterator first, FwIterator last) {
  FwIterator min = first;
  while (first < last) {
    if (*first < *min) {
      min = first;
    }
    first++;
  }
  return min;
}

// Returns an iterator to the minimum element in the range [|first|, |last|)
// using |comp| to compare elements instead of operator<
//
// |first| and |last| must be forward iterators.
//
// Similar to: <http://en.cppreference.com/w/cpp/algorithm/min_element>
template <class FwIterator, class Compare>
FwIterator min_element(FwIterator first, FwIterator last, Compare comp) {
  FwIterator min = first;
  while (first < last) {
    if (comp(*first, *min)) {
      min = first;
    }
    first++;
  }
  return min;
}

// Returns a pointer to the first element that is not less than |value|, or
// |last| if no such element is found.
//
// Similar to <http://en.cppreference.com/w/cpp/algorithm/lower_bound>
template <class T, class U>
const T* lower_bound(const T* first, const T* last, const U& value) {
  while (first < last) {
    const T* probe = first + (last - first) / 2;
    if (*probe < value) {
      first = probe + 1;
    } else {
      last = probe;
    }
  }
  return last;
}

// Returns a pointer to the first element that is not less than |value|, or
// |last| if no such element is found.
//
// |comp| is used to compare the elements rather than operator<.
//
// Similar to <http://en.cppreference.com/w/cpp/algorithm/lower_bound>
template <class T, class U, class Compare>
const T* lower_bound(const T* first, const T* last, const U& value, Compare comp) {
  while (first < last) {
    const T* probe = first + (last - first) / 2;
    if (comp(*probe, value)) {
      first = probe + 1;
    } else {
      last = probe;
    }
  }
  return last;
}

template <typename T, size_t N>
constexpr size_t count_of(T const (&)[N]) {
  return N;
}

}  // namespace fbl

#endif  // FBL_ALGORITHM_H_

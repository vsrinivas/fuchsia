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
  return val == 0               ? 0
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

template <typename T, size_t N>
constexpr size_t count_of(T const (&)[N]) {
  return N;
}

}  // namespace fbl

#endif  // FBL_ALGORITHM_H_

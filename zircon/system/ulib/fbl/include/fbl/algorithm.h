// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FBL_ALGORITHM_H_
#define FBL_ALGORITHM_H_

#include <lib/stdcompat/bit.h>
#include <stdlib.h>

#include <algorithm>
#include <limits>
#include <type_traits>

namespace fbl {

// round_up rounds up val until it is divisible by multiple.
// Zero is divisible by all multiples.
template <class T, class U, class L = std::conditional_t<sizeof(T) >= sizeof(U), T, U>,
          class = std::enable_if_t<std::is_unsigned_v<T>>,
          class = std::enable_if_t<std::is_unsigned_v<U>>>
constexpr const L round_up(const T& val_, const U& multiple_) {
  const L val = static_cast<L>(val_);
  const L multiple = static_cast<L>(multiple_);
  return val == 0                             ? 0
         : cpp20::has_single_bit<L>(multiple) ? (val + (multiple - 1)) & ~(multiple - 1)
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
  return val == 0                             ? 0
         : cpp20::has_single_bit<L>(multiple) ? val & ~(multiple - 1)
                                              : (val / multiple) * multiple;
}

// Rounds up to the nearest power of 2.
// - 0 is not considered a power of 2 as there is no X such that 2**X = 0.
//   fbl::roundup(0) == 1
// - If val is a power of 2 then val is returned.
// - An input greater than the most significant bit set for that type is
//   undefined behavior and will assert.
template <class T>
constexpr T roundup_pow2(T val) {
  static_assert(std::is_same_v<T, uint32_t> || std::is_same_v<T, uint64_t>,
                "fbl::roundup_pow2() only supports uint32_t & uint64_t");

  if (val <= 1) {
    return 1;
  }

  constexpr T limit = 1ULL << (std::numeric_limits<T>::digits - 1);
  if (val > limit) {
    // val exceeded the maximum power of 2 supported by the type.
    __builtin_abort();
  }

  constexpr T kOne = static_cast<T>(1);
  if constexpr (std::is_same<T, uint64_t>::value) {
    return kOne << (std::numeric_limits<T>::digits - __builtin_clzl(val - 1));
  } else {
    return kOne << (std::numeric_limits<T>::digits - __builtin_clz(val - 1));
  }
}

}  // namespace fbl

#endif  // FBL_ALGORITHM_H_

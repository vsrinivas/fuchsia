// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_LIB_DEV_OPERATION_INCLUDE_LIB_OPERATION_HELPERS_ALGORITHM_H_
#define SRC_DEVICES_LIB_DEV_OPERATION_INCLUDE_LIB_OPERATION_HELPERS_ALGORITHM_H_

#include <stdlib.h>

#include <algorithm>
#include <limits>
#include <type_traits>

namespace operation {

// is_pow2<T>(T val)
//
// Tests to see if val (which may be any unsigned integer type) is a power of 2
// or not.  0 is not considered to be a power of 2.
//
template <typename T, typename = std::enable_if_t<std::is_unsigned_v<T>>>
constexpr bool is_pow2(T val) {
  return (val != 0) && (((val - 1U) & val) == 0);
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

}  // namespace operation

#endif  // SRC_DEVICES_LIB_DEV_OPERATION_INCLUDE_LIB_OPERATION_HELPERS_ALGORITHM_H_

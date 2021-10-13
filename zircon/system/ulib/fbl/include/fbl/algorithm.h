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

}  // namespace fbl

#endif  // FBL_ALGORITHM_H_

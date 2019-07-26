// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_AFFINE_UTILS_H_
#define LIB_AFFINE_UTILS_H_

#include <limits>
#include <stdint.h>
#include <zircon/compiler.h>

namespace affine {
namespace utils {

// Simple wrappers around the compiler built-in add/sub overflow routines which
// implement a clamping policy in the case of overflow for int64_t.

inline int64_t ClampAdd(int64_t a, int64_t b) {
  int64_t ret;
  if (unlikely(add_overflow(a, b, &ret))) {
    return (b < 0) ? std::numeric_limits<int64_t>::min() : std::numeric_limits<int64_t>::max();
  }
  return ret;
}

inline int64_t ClampSub(int64_t a, int64_t b) {
  int64_t ret;
  if (unlikely(sub_overflow(a, b, &ret))) {
    return (b < 0) ? std::numeric_limits<int64_t>::max() : std::numeric_limits<int64_t>::min();
  }
  return ret;
}

}  // namespace utils
}  // namespace affine

#endif

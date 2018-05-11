// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ESCHER_UTIL_BIT_OPS_H_
#define LIB_ESCHER_UTIL_BIT_OPS_H_

#include <cstdint>

namespace escher {

// Use compiler built-ins, if available.
#if defined(__clang__) || defined(__GCC__)
inline int32_t CountLeadingZeros(uint32_t value) {
  return value == 0 ? 32 : __builtin_clz(value);
}
inline int32_t CountTrailingZeros(uint32_t value) {
  return value == 0 ? 32 : __builtin_ctz(value);
}
#else
inline int32_t CountLeadingZeros(uint32_t value) {
  constexpr uint32_t mask = 1 << 31;
  int32_t count = 0;
  while (value != 0) {
    if (value & mask) {
      return count;
    }
    count += 1;
    value = value << 1;
  }
  return 32;
}
inline int32_t CountTrailingZeros(uint32_t value) {
  int32_t count = 0;
  while (value != 0) {
    if (value & 1) {
      return count;
    }
    count += 1;
    value = value >> 1;
  }
  return 32;
}
#endif  // #if defined(__clang__) || defined(__GCC__)

inline int32_t CountLeadingOnes(uint32_t value) {
  return CountLeadingZeros(~value);
}

inline int32_t CountTrailingOnes(uint32_t value) {
  return CountTrailingZeros(~value);
}

template <typename T>
inline void ForEachBitIndex(uint32_t value, const T& func) {
  while (value) {
    uint32_t bit = CountTrailingZeros(value);
    func(bit);
    value &= ~(1u << bit);
  }
}

}  // namespace escher

#endif  // LIB_ESCHER_UTIL_BIT_OPS_H_

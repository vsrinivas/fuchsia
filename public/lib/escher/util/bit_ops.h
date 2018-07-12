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
inline uint32_t CountOnes(uint32_t value) {
  return uint32_t(__builtin_popcount(value));
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
inline uint32_t CountOnes(uint32_t value) {
  uint32_t count = 0;
  while (value != 0) {
    count += value & 1;
    value = value >> 1;
  }
  return count;
}
#endif  // #if defined(__clang__) || defined(__GCC__)

inline int32_t CountLeadingOnes(uint32_t value) {
  return CountLeadingZeros(~value);
}

inline int32_t CountTrailingOnes(uint32_t value) {
  return CountTrailingZeros(~value);
}

// Invoke |func| with the index of each non-zero bit in |value|.
template <typename T>
inline void ForEachBitIndex(uint32_t value, const T& func) {
  while (value) {
    uint32_t bit = CountTrailingZeros(value);
    func(bit);
    value &= ~(1u << bit);
  }
}

// Invoke |func| for each contiguous range of non-zero bits in |value|.  Two
// arguments are passed to each invocation of |func|:
// - the index of the initial bit of the range
// - the number of bits in the range
template <typename T>
inline void ForEachBitRange(uint32_t value, const T& func) {
  while (value) {
    // Find the first non-zero bit.
    uint32_t bit = CountTrailingZeros(value);
    // Starting with that bit, count the number of contiguous non-zero bits.
    uint32_t range = CountTrailingOnes(value >> bit);
    func(bit, range);
    // Prepare for the next iteration by zeroing out the non-zero range that
    // was just found.
    value &= ~((1u << (bit + range)) - 1);
  }
}

// Set to 1 all bits in |input| at and above |index|.
template <typename T>
inline void SetBitsAtAndAboveIndex(T* input, uint32_t index) {
  *input |= ~((T{1} << index) - 1);
}

}  // namespace escher

#endif  // LIB_ESCHER_UTIL_BIT_OPS_H_

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_VMM_BITS_H_
#define SRC_VIRTUALIZATION_BIN_VMM_BITS_H_

#include <stdint.h>

template <typename T>
static inline constexpr T bit_mask(size_t bits) {
  if (bits >= sizeof(T) * 8) {
    return static_cast<T>(-1ull);
  }
  return static_cast<T>((1ull << bits) - 1ull);
}

template <typename T>
static inline constexpr T clear_bits(T x, size_t nbits, size_t shift) {
  return x & ~(bit_mask<T>(nbits) << shift);
}

template <typename T>
static inline constexpr T set_bits(T x, size_t high, size_t low) {
  return static_cast<T>((x & ((1 << (high - low)) - 1)) << low);
}

template <typename T>
static inline constexpr T bit_shift(T x, size_t bit) {
  return static_cast<T>((x >> bit) & 1);
}

template <typename T>
static inline constexpr T bits_shift(T x, size_t high, size_t low) {
  return static_cast<T>((x >> low) & bit_mask<T>(high - low + 1));
}

template <typename T>
static inline constexpr T round_up(T x, size_t alignment) {
  auto mask = static_cast<T>(alignment - 1);
  return (x + mask) & ~mask;
}

template <typename T>
static inline constexpr T round_down(T x, size_t alignment) {
  auto mask = static_cast<T>(alignment - 1);
  return x & ~mask;
}

template <typename T>
static inline constexpr T align(T x, size_t alignment) {
  return round_up<T>(x, alignment);
}

template <typename T>
static inline constexpr bool is_aligned(T x, size_t alignment) {
  auto mask = static_cast<T>(alignment - 1);
  return (x & mask) == 0;
}

#endif  // SRC_VIRTUALIZATION_BIN_VMM_BITS_H_

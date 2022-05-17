// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_POW2_H_
#define ZIRCON_KERNEL_INCLUDE_POW2_H_

#include <stdint.h>
#include <sys/types.h>

#include <ktl/bit.h>

constexpr bool ispow2(uint val) { return val == 0 || ktl::has_single_bit(val); }

// Compute floor(log2(|val|)), or 0 if |val| is 0
template <typename T>
constexpr T log2_floor(T val) {
  return val == 0 ? 0 : ktl::bit_width(val) - 1;
}
constexpr uint log2_uint_floor(uint val) { return log2_floor(val); }
constexpr ulong log2_ulong_floor(ulong val) { return log2_floor(val); }

// Compute ceil(log2(|val|)), or 0 if |val| is 0
template <typename T>
constexpr T log2_ceil(T val) {
  T log2 = log2_floor(val);
  if (val != 0 && val != (static_cast<T>(1) << log2)) {
    ++log2;
  }
  return log2;
}
constexpr uint log2_uint_ceil(uint val) { return log2_ceil(val); }
constexpr ulong log2_ulong_ceil(ulong val) { return log2_ceil(val); }

template <typename T>
constexpr T valpow2(T valp2) {
  return static_cast<T>(1) << valp2;
}

constexpr uint divpow2(uint val, uint divp2) { return val >> divp2; }

constexpr uint modpow2(uint val, uint modp2) { return val & ((1U << modp2) - 1); }

constexpr uint64_t modpow2_u64(uint64_t val, uint modp2) { return val & ((1U << modp2) - 1); }

constexpr uint32_t round_up_pow2_u32(uint32_t v) {
  return (v == 0 || v > (1u << 31)) ? 0u : ktl::bit_ceil(v);
}

#endif  // ZIRCON_KERNEL_INCLUDE_POW2_H_

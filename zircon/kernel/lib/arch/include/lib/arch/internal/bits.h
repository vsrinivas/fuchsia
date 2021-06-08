// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_INTERNAL_BITS_H_
#define ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_INTERNAL_BITS_H_

#include <stdlib.h>
#include <zircon/assert.h>

#include <type_traits>

namespace arch::internal {

// Extracts the bit range [high_bit:low_bit] (inclusive) from a numerical input.
template <typename T>
constexpr inline T ExtractBits(size_t high_bit, size_t low_bit, T input) {
  static_assert(std::is_integral_v<T>, "Require T to be an integral type.");
  ZX_DEBUG_ASSERT_MSG(high_bit >= low_bit, "High bit must be greater or equal to low bit.");
  ZX_DEBUG_ASSERT_MSG(high_bit < (sizeof(T) * 8), "Source value ends before high bit");

  const size_t bit_count = high_bit + 1 - low_bit;  // +1 for inclusivity of the upper bound.
  const T pow2 = static_cast<T>(1) << bit_count;
  return static_cast<T>((input >> low_bit) & (pow2 - 1));
}

// Replaces the bits in range [high_bit:low_bit] (inclusive) with the new value `replacement`.
template <typename T>
constexpr inline T UpdateBits(size_t high_bit, size_t low_bit, T input, T replacement) {
  static_assert(std::is_integral_v<T>, "Require T to be an integral type.");
  ZX_DEBUG_ASSERT_MSG(high_bit >= low_bit, "High bit must be greater or equal to low bit.");
  ZX_DEBUG_ASSERT_MSG(high_bit < (sizeof(T) * 8), "Source value ends before high bit");

  const size_t bit_count = high_bit + 1 - low_bit;  // +1 for inclusivity of the upper bound.
  const T pow2 = static_cast<T>(1) << bit_count;
  ZX_DEBUG_ASSERT_MSG(replacement <= (pow2 - 1), "Replacement value too large to fit in range.");
  T mask = pow2 << low_bit;
  return static_cast<T>((input & ~mask) | (replacement << low_bit));
}

}  // namespace arch::internal

#endif  // ZIRCON_KERNEL_LIB_ARCH_INCLUDE_LIB_ARCH_INTERNAL_BITS_H_

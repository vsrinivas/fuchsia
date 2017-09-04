// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#define _ONE(x) (1 + ((x) - (x)))

#define BIT(x, bit) ((x) & (_ONE(x) << (bit)))
#define BIT_SHIFT(x, bit) (((x) >> (bit)) & 1)
#define BITS(x, high, low) ((x) & (((_ONE(x) << ((high) + 1)) - 1) & ~((_ONE(x) << (low)) - 1)))
#define BIT_SET(x, bit) (((x) & (_ONE(x) << (bit))) ? 1 : 0)

// From: https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
static constexpr inline uint32_t round_up_pow2(uint32_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return ++v;
}

template <typename T>
static inline constexpr T bit_mask(size_t bits) {
    if (bits >= sizeof(T) * 8)
        return static_cast<T>(0) - 1;
    return static_cast<T>((1 << bits) - 1);
}

template <typename T>
static inline constexpr T clear_bits(T x, size_t nbits, size_t shift) {
    return x & ~(bit_mask<T>(nbits) << shift);
}

template <typename T>
static inline constexpr T bits_shift(T x, size_t high, size_t low) {
    return (x >> low) & bit_mask<T>(high - low + 1);
}

template <typename T>
static inline constexpr T align(T x, size_t alignment) {
    auto mask = static_cast<T>(alignment - 1);
    return static_cast<T>(x + mask) & static_cast<T>(~mask);
}

template <typename T>
static inline constexpr bool is_aligned(T x, size_t alignment) {
    auto mask = static_cast<T>(alignment - 1);
    return (x & mask) == 0;
}

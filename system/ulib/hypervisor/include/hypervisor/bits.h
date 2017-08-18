// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#define _ONE(x) (1 + ((x) - (x)))

#define BIT(x, bit) ((x) & (_ONE(x) << (bit)))
#define BIT_SHIFT(x, bit) (((x) >> (bit)) & 1)
#define BITS(x, high, low) ((x) & (((_ONE(x) << ((high) + 1)) - 1) & ~((_ONE(x) << (low)) - 1)))
#define BITS_SHIFT(x, high, low) (((x) >> (low)) & ((_ONE(x) << ((high) - (low) + 1)) - 1))
#define BIT_SET(x, bit) (((x) & (_ONE(x) << (bit))) ? 1 : 0)

#define BIT_MASK(x) (((x) >= sizeof(unsigned long) * 8) ? (0UL - 1) : ((1UL << (x)) - 1))
#define CLEAR_BITS(x, nbits, shift) ((x) & (~(((1UL << (nbits)) - 1) << (shift))))

// From: https://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
static inline uint32_t round_up_pow2(uint32_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return ++v;
}

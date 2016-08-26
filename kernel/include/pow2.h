// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef __POW2_H
#define __POW2_H

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include <magenta/compiler.h>

__BEGIN_CDECLS

/* routines for dealing with power of 2 values for efficiency */
static inline __ALWAYS_INLINE bool ispow2(uint val)
{
    return ((val - 1) & val) == 0;
}

static inline __ALWAYS_INLINE uint log2_uint(uint val)
{
    if (val == 0)
        return 0; // undefined

    return (uint)(sizeof(val) * 8) - 1 - __builtin_clz(val);
}

static inline __ALWAYS_INLINE uint log2_uint_roundup(uint val)
{
    if (val == 0)
        return 0; // undefined

    // log2 of anything greater than a uint with just the MSB set will round up
    // to number-of-bits-in-uint.
    if (val > (1u << ((sizeof(val) * 8) - 1)))
        return (sizeof(val) * 8);

    val = (val << 1) - 1;

    return (uint)(sizeof(val) * 8) - 1 - __builtin_clz(val);
}

static inline __ALWAYS_INLINE uint valpow2(uint valp2)
{
    return 1U << valp2;
}

static inline __ALWAYS_INLINE uint divpow2(uint val, uint divp2)
{
    return val >> divp2;
}

static inline __ALWAYS_INLINE uint modpow2(uint val, uint modp2)
{
    return val & ((1U << modp2) - 1);
}

static inline __ALWAYS_INLINE uint64_t modpow2_u64(uint64_t val, uint modp2)
{
    return val & ((1U << modp2) - 1);
}

// Cribbed from:
// http://graphics.stanford.edu/~seander/bithacks.html#RoundUpPowerOf2
static inline __ALWAYS_INLINE uint32_t round_up_pow2_u32(uint32_t v)
{
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

__END_CDECLS

#endif


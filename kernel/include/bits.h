// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <magenta/compiler.h>
#include <arch/ops.h>

__BEGIN_CDECLS

#define clz(x) __builtin_clz(x)
#define ctz(x) __builtin_ctz(x)

/* Trick to get a 1 of the right size */
#define _ONE(x) (1 + ((x) - (x)))

#define BIT(x, bit) ((x) & (_ONE(x) << (bit)))
#define BIT_SHIFT(x, bit) (((x) >> (bit)) & 1)
#define BITS(x, high, low) ((x) & (((_ONE(x)<<((high)+1))-1) & ~((_ONE(x)<<(low))-1)))
#define BITS_SHIFT(x, high, low) (((x) >> (low)) & ((_ONE(x)<<((high)-(low)+1))-1))
#define BIT_SET(x, bit) (((x) & (_ONE(x) << (bit))) ? 1 : 0)

#define BITMAP_BITS_PER_WORD ((int)(sizeof(unsigned long) * 8))
#define BITMAP_NUM_WORDS(x) (((x) + BITMAP_BITS_PER_WORD - 1) / BITMAP_BITS_PER_WORD)
#define BITMAP_WORD(x) ((x) / BITMAP_BITS_PER_WORD)
#define BITMAP_BIT_IN_WORD(x) ((x) & (BITMAP_BITS_PER_WORD - 1))

#define BITMAP_FIRST_WORD_MASK(start) (~0UL << ((start) % BITMAP_BITS_PER_WORD))
#define BITMAP_LAST_WORD_MASK(nbits) (((nbits) % BITMAP_BITS_PER_WORD) ? (1UL<<((nbits) % BITMAP_BITS_PER_WORD))-1 : ~0UL)

#define BITMAP_BITS_PER_INT (sizeof(unsigned int) * 8)
#define BITMAP_BIT_IN_INT(x) ((x) & (BITMAP_BITS_PER_INT - 1))
#define BITMAP_INT(x) ((x) / BITMAP_BITS_PER_INT)

#define BIT_MASK(x) (((x) >= sizeof(unsigned long) * 8) ? (0UL-1) : ((1UL << (x)) - 1))

static inline void bitmap_set(unsigned long *bitmap, int start, int nr)
{
    unsigned long *p = bitmap + BITMAP_WORD(start);
    const long size = start + nr;
    int bits_to_set = BITMAP_BITS_PER_WORD - (start % BITMAP_BITS_PER_WORD);
    unsigned long mask_to_set = BITMAP_FIRST_WORD_MASK(start);

    while (nr - bits_to_set >= 0) {
        *p |= mask_to_set;
        nr -= bits_to_set;
        bits_to_set = BITMAP_BITS_PER_WORD;
        mask_to_set = ~0UL;
        p++;
    }
    if (nr) {
        mask_to_set &= BITMAP_LAST_WORD_MASK(size);
        *p |= mask_to_set;
    }
}

static inline void bitmap_clear(unsigned long *bitmap, int start, int nr)
{
    unsigned long *p = bitmap + BITMAP_WORD(start);
    const long size = start + nr;
    int bits_to_clear = BITMAP_BITS_PER_WORD - (start % BITMAP_BITS_PER_WORD);
    unsigned long mask_to_clear = BITMAP_FIRST_WORD_MASK(start);

    while (nr - bits_to_clear >= 0) {
        *p &= ~mask_to_clear;
        nr -= bits_to_clear;
        bits_to_clear = BITMAP_BITS_PER_WORD;
        mask_to_clear = ~0UL;
        p++;
    }
    if (nr) {
        mask_to_clear &= BITMAP_LAST_WORD_MASK(size);
        *p &= ~mask_to_clear;
    }
}

static inline int bitmap_test(unsigned long *bitmap, int bit)
{
    return BIT_SET(bitmap[BITMAP_WORD(bit)], BITMAP_BIT_IN_WORD(bit));
}

/* find first zero bit starting from LSB */
static inline unsigned long _ffz(unsigned long x)
{
    return __builtin_ffsl(~x) - 1;
}

static inline int bitmap_ffz(unsigned long *bitmap, int numbits)
{
    int bit;

    for (int i = 0; i < BITMAP_NUM_WORDS(numbits); i++) {
        if (bitmap[i] == ~0UL)
            continue;
        bit = i * BITMAP_BITS_PER_WORD + (int)_ffz(bitmap[i]);
        if (bit < numbits)
            return bit;
        return -1;
    }
    return -1;
}

__END_CDECLS

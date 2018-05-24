/*
 * Copyright 2018 The Fuchsia Authors.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once

#include <zircon/assert.h>

#define ASSERT_MTX_HELD(mtx) ZX_ASSERT(mtx_trylock(mtx) != thrd_success)

#define BITARR_TYPE uint64_t
#define BITARR_TYPE_NUM_BITS (sizeof(BITARR_TYPE) * 8)
#define BITARR(name, num_bits) \
            BITARR_TYPE name[DIV_ROUNDUP(num_bits, BITARR_TYPE_NUM_BITS)]
#define BITARR_SET(name, bit) \
            (name)[(bit) / BITARR_TYPE_NUM_BITS] |= \
                ((BITARR_TYPE)1 << ((bit) % BITARR_TYPE_NUM_BITS))
#define BITARR_CLEAR(name, bit) \
            (name)[(bit) / BITARR_TYPE_NUM_BITS] &= \
                ~((BITARR_TYPE)1 << ((bit) % BITARR_TYPE_NUM_BITS))
#define BITARR_TEST(name, bit) \
            (((name)[(bit) / BITARR_TYPE_NUM_BITS] & \
                ((BITARR_TYPE)1 << ((bit) % BITARR_TYPE_NUM_BITS))) != 0)

#define BITMASK1(val) ((1UL << (val)) - 1)
#define BITMASK(lo, hi) ((BITMASK1((hi) + 1) & ~BITMASK1(lo)))

#define COND_WARN1(cond, filename, lineno) \
    ath10k_warn("unexpected condition %s at %s:%d\n", cond, filename, lineno)
#define COND_WARN(cond)                               \
    ({                                                \
        bool result = cond;                           \
        if (result) {                                 \
            COND_WARN1(#cond, __FILE__, __LINE__);    \
        }                                             \
        result;                                       \
    })
#define WARN_ONCE()                                                                     \
    do {                                                                                \
        static bool warn_next = true;                                                   \
        if (warn_next) {                                                                \
            ath10k_warn("code at %s:%d not expected to execute\n", __FILE__, __LINE__); \
            warn_next = false;                                                          \
        }                                                                               \
    } while (0)
#define COND_WARN_ONCE(cond)                          \
    ({                                                \
        static bool warn_next = true;                 \
        bool result = cond;                           \
        if (result && warn_next) {                    \
            COND_WARN1(#cond, __FILE__, __LINE__);    \
            warn_next = false;                        \
        }                                             \
        result;                                       \
    })

#define DIV_ROUNDUP(n, m) (((n) + ((m) - 1)) / (m))

#define IS_ALIGNED(a, b) (!(((uintptr_t)(a)) & (((uintptr_t)(b))-1)))

#define IS_POW2(x) (((x) != 0) && (((x) & ((x) - 1)) == 0))

#define LOG2(val)  \
    (((val) == 0) ? 0 : (((sizeof(unsigned long long) * 8) - 1) - __builtin_clzll(val)))

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MIN_T(t,a,b) (((t)(a) < (t)(b)) ? (t)(a) : (t)(b))

#define READ32(addr) (*(volatile uint32_t*)(uintptr_t)(addr))
#define WRITE32(addr, value)                                  \
    do {                                                      \
        (*(volatile uint32_t*)(uintptr_t)(addr)) = (value);   \
    } while (0)

#define ROUNDUP_POW2(val) \
    ((unsigned long) (val) == 0 ? (val) : \
             1UL << ((sizeof(unsigned long) * 8) - __builtin_clzl((val) - 1)))
#define ROUNDUP_LOG2(val) \
    ((unsigned long) (val) == 0 ? (val) : \
             ((sizeof(unsigned long) * 8) - __builtin_clzl((val) - 1)))

// Similar to snprintf, but returns actual size used, not size needed
#define SNPRINTF_USED(buf, size, format, ...)                       \
    ({                                                              \
        int result = snprintf(buf, size, format, __VA_ARGS__);      \
        MIN_T(int, size, result);                                   \
    })

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_COMMON_MACROS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_COMMON_MACROS_H_

//
//
//

#include <assert.h>
#include <stdint.h>

//
// clang-format off
//

#define ARRAY_LENGTH_MACRO(x)   (sizeof(x)/sizeof(x[0]))
#define OFFSET_OF_MACRO(t,m)    ((size_t)&(((t*)0)->m))
#define MEMBER_SIZE_MACRO(t,m)  sizeof(((t*)0)->m)

//
// FIXME
//
// Consider providing typed min/max() functions:
//
//   <type> [min|max]_<type>(a,b) { ; }
//
// But note we still need preprocessor-time min/max().
//

#define MAX_MACRO(t,a,b)  (((a) > (b)) ? (a) : (b))
#define MIN_MACRO(t,a,b)  (((a) < (b)) ? (a) : (b))

//
//
//

#define BITS_TO_MASK_MACRO(n)         (((uint32_t)1<<(n))-1)
#define BITS_TO_MASK_64_MACRO(n)      (((uint64_t)1<<(n))-1)

#define BITS_TO_MASK_AT_MACRO(n,b)    (BITS_TO_MASK_MACRO(n)<<(b))
#define BITS_TO_MASK_AT_64_MACRO(n,b) (BITS_TO_MASK_64_MACRO(n)<<(b))

//
//
//

#define STRINGIFY_MACRO_2(a)          #a
#define STRINGIFY_MACRO(a)            STRINGIFY_MACRO_2(a)

//
//
//

#define CONCAT_MACRO_2(a,b)           a ## b
#define CONCAT_MACRO(a,b)             CONCAT_MACRO_2(a,b)

//
// Round up/down
//

#define ROUND_DOWN_MACRO(v,q)         (((v) / (q)) * (q))
#define ROUND_UP_MACRO(v,q)           ((((v) + (q) - 1) / (q)) * (q))

//
// Round up/down when q is a power-of-two.
//

#define ROUND_DOWN_POW2_MACRO(v,q)    ((v) & ~((q) - 1))
#define ROUND_UP_POW2_MACRO(v,q)      ROUND_DOWN_POW2_MACRO((v) + (q) - 1,q)

//
// Convert byte pointer to a network order 32-bit integer to host
// order.
//

#define NPBTOHL_MACRO(pb4)                                              \
  ((((pb4)[0])<<24) | (((pb4)[1])<<16) | (((pb4)[2])<< 8) | (pb4)[3])

//
// FIXME -- get rid of network order counts in target_config.
// It will be OK to assume little-endian.
//

#if defined (_MSC_VER) && !defined (__clang__)

#if REG_DWORD == REG_DWORD_LITTLE_ENDIAN
#define NTOHL_MACRO(x)  _byteswap_ulong(x)
#else
#define NTOHL_MACRO(x)  x
#endif

#else // clang/gcc
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define NTOHL_MACRO(x)  __builtin_bswap32(x)
#else
#define NTOHL_MACRO(x)  x
#endif

#endif

//
//
//

#if defined (_MSC_VER) && !defined (__clang__)
#define ALLOCA_MACRO(n)  _alloca(n)
#else
#define ALLOCA_MACRO(n)  __builtin_alloca(n)
#endif

//
//
//

#if defined (_MSC_VER) && !defined (__clang__)
#define STATIC_ASSERT_MACRO(c,m) static_assert(c,m)
#else
#define STATIC_ASSERT_MACRO(c,m) _Static_assert(c,m)
#endif

#define STATIC_ASSERT_MACRO_1(c) STATIC_ASSERT_MACRO(c,#c)

//
//
//

#if defined (_MSC_VER) && !defined (__clang__)
#define POPCOUNT_MACRO(...) __popcnt(__VA_ARGS__)
#else
#define POPCOUNT_MACRO(...) __builtin_popcount(__VA_ARGS__)
#endif

//
//
//

#if defined (_MSC_VER) && !defined (__clang__)
#define ALIGN_MACRO(_bytes)  __declspec(align(_bytes)) // only accepts integer as arg
#else
#include <stdalign.h>
#define ALIGN_MACRO(_bytes)  alignas(_bytes)
#endif

//
// clang-format on
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_COMMON_MACROS_H_

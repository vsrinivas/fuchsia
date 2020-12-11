// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_COMMON_MACROS_H_
#define SRC_GRAPHICS_LIB_COMPUTE_COMMON_MACROS_H_

//
//
//

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

//
// clang-format off
//

#define ARRAY_LENGTH_MACRO(x_)          (sizeof(x_)/sizeof(x_[0]))
#define OFFSETOF_MACRO(t_,m_)           offsetof(t_,m_)
#define MEMBER_SIZE_MACRO(t_,m_)        sizeof(((t_*)0)->m_)

//
// FIXME(allanmac):
//
// Consider providing typed min/max() functions:
//
//   <type> [min|max]_<type>(a,b) { ; }
//
// But note we still need preprocessor-time min/max().
//

#define MAX_MACRO(t_,a_,b_)             (((a_) > (b_)) ? (a_) : (b_))
#define MIN_MACRO(t_,a_,b_)             (((a_) < (b_)) ? (a_) : (b_))

//
//
//

#define BITS_TO_MASK_MACRO(n_)          (((uint32_t)1<<(n_))-1)
#define BITS_TO_MASK_64_MACRO(n_)       (((uint64_t)1<<(n_))-1)

#define BITS_TO_MASK_AT_MACRO(n_,b_)    (BITS_TO_MASK_MACRO(n_)   <<(b_))
#define BITS_TO_MASK_AT_64_MACRO(n_,b_) (BITS_TO_MASK_64_MACRO(n_)<<(b_))

//
//
//

#define STRINGIFY_MACRO_2(a_)           #a_
#define STRINGIFY_MACRO(a_)             STRINGIFY_MACRO_2(a_)

//
//
//

#define CONCAT_MACRO_2(a_,b_)           a_ ## b_
#define CONCAT_MACRO(a_,b_)             CONCAT_MACRO_2(a_,b_)

//
// Round up/down
//

#define ROUND_DOWN_MACRO(v_,q_)         (((v_) / (q_)) * (q_))
#define ROUND_UP_MACRO(v_,q_)           ((((v_) + (q_) - 1) / (q_)) * (q_))

//
// Round up/down when q is a power-of-two.
//

#define ROUND_DOWN_POW2_MACRO(v_,q_)    ((v_) & ~((q_) - 1))
#define ROUND_UP_POW2_MACRO(v_,q_)      ROUND_DOWN_POW2_MACRO((v_) + (q_) - 1, q_)

//
// Convert byte pointer to a network order 32-bit integer to host
// order.
//

#define NPBTOHL_MACRO(pb4_)             ((((pb4_)[0]) << 24) | \
                                         (((pb4_)[1]) << 16) | \
                                         (((pb4_)[2]) <<  8) | \
                                           (pb4_)[3])

//
// FIXME(allanmac): get rid of network order counts in target_config.
// It will be OK to assume little-endian.
//

#if defined (_MSC_VER) && !defined (__clang__)

#if REG_DWORD == REG_DWORD_LITTLE_ENDIAN
#define NTOHL_MACRO(x_)  _byteswap_ulong(x_)
#else
#define NTOHL_MACRO(x_)  x
#endif

#else // clang/gcc
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define NTOHL_MACRO(x_)  __builtin_bswap32(x_)
#else
#define NTOHL_MACRO(x_)  x
#endif

#endif

//
// FIXME(allanmac): we won't be using these
//

#if defined (_MSC_VER) && !defined (__clang__)
#define ALLOCA_MACRO(n_)  _alloca(n_)
#else
#define ALLOCA_MACRO(n_)  __builtin_alloca(n_)
#endif

//
//
//

#if defined (_MSC_VER) && !defined (__clang__)
#define STATIC_ASSERT_MACRO(c_,m_) static_assert(c_,m_)
#else
#define STATIC_ASSERT_MACRO(c_,m_) _Static_assert(c_,m_)
#endif

#define STATIC_ASSERT_MACRO_1(c_) STATIC_ASSERT_MACRO(c_,#c_)

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
#define ALIGN_MACRO(bytes_)  __declspec(align(bytes_)) // only accepts integer as arg
#else
#include <stdalign.h>
#define ALIGN_MACRO(bytes_)  alignas(bytes_)
#endif

//
// clang-format on
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_COMMON_MACROS_H_

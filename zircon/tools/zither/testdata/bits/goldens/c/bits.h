// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// DO NOT EDIT. Generated from FIDL library
//   zither.bits (//zircon/tools/zither/testdata/bits/bits.test.fidl)
// by zither, a Fuchsia platform tool.

#ifndef LIB_ZITHER_BITS_C_BITS_H_
#define LIB_ZITHER_BITS_C_BITS_H_

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef uint8_t zither_bits_uint8_bits_t;

#define ZITHER_BITS_UINT8_BITS_ONE ((zither_bits_uint8_bits_t)(1u << 0))
#define ZITHER_BITS_UINT8_BITS_TWO ((zither_bits_uint8_bits_t)(1u << 1))
#define ZITHER_BITS_UINT8_BITS_FOUR ((zither_bits_uint8_bits_t)(1u << 2))
#define ZITHER_BITS_UINT8_BITS_EIGHT ((zither_bits_uint8_bits_t)(1u << 3))
#define ZITHER_BITS_UINT8_BITS_SIXTEEN ((zither_bits_uint8_bits_t)(1u << 4))
#define ZITHER_BITS_UINT8_BITS_THIRTY_TWO ((zither_bits_uint8_bits_t)(1u << 5))
#define ZITHER_BITS_UINT8_BITS_SIXTY_FOUR ((zither_bits_uint8_bits_t)(1u << 6))
#define ZITHER_BITS_UINT8_BITS_ONE_HUNDRED_TWENTY_EIGHT ((zither_bits_uint8_bits_t)(1u << 7))

typedef uint16_t zither_bits_uint16_bits_t;

#define ZITHER_BITS_UINT16_BITS_ZEROTH ((zither_bits_uint16_bits_t)(1u << 0))
#define ZITHER_BITS_UINT16_BITS_FIRST ((zither_bits_uint16_bits_t)(1u << 1))
#define ZITHER_BITS_UINT16_BITS_SECOND ((zither_bits_uint16_bits_t)(1u << 2))
#define ZITHER_BITS_UINT16_BITS_THIRD ((zither_bits_uint16_bits_t)(1u << 3))
#define ZITHER_BITS_UINT16_BITS_FOURTH ((zither_bits_uint16_bits_t)(1u << 4))
#define ZITHER_BITS_UINT16_BITS_FIFTH ((zither_bits_uint16_bits_t)(1u << 5))
#define ZITHER_BITS_UINT16_BITS_SIXTH ((zither_bits_uint16_bits_t)(1u << 6))
#define ZITHER_BITS_UINT16_BITS_SEVENTH ((zither_bits_uint16_bits_t)(1u << 7))
#define ZITHER_BITS_UINT16_BITS_EIGHT ((zither_bits_uint16_bits_t)(1u << 8))
#define ZITHER_BITS_UINT16_BITS_NINTH ((zither_bits_uint16_bits_t)(1u << 9))
#define ZITHER_BITS_UINT16_BITS_TENTH ((zither_bits_uint16_bits_t)(1u << 10))
#define ZITHER_BITS_UINT16_BITS_ELEVENTH ((zither_bits_uint16_bits_t)(1u << 11))
#define ZITHER_BITS_UINT16_BITS_TWELFTH ((zither_bits_uint16_bits_t)(1u << 12))
#define ZITHER_BITS_UINT16_BITS_THIRTEENTH ((zither_bits_uint16_bits_t)(1u << 13))
#define ZITHER_BITS_UINT16_BITS_FOURTEENTH ((zither_bits_uint16_bits_t)(1u << 14))
#define ZITHER_BITS_UINT16_BITS_FIFTHTEENTH ((zither_bits_uint16_bits_t)(1u << 15))

typedef uint32_t zither_bits_uint32_bits_t;

#define ZITHER_BITS_UINT32_BITS_POW_0 ((zither_bits_uint32_bits_t)(1u << 0))
#define ZITHER_BITS_UINT32_BITS_POW_31 ((zither_bits_uint32_bits_t)(1u << 31))

typedef uint64_t zither_bits_uint64_bits_t;

#define ZITHER_BITS_UINT64_BITS_POW_0 ((zither_bits_uint64_bits_t)(1u << 0))
#define ZITHER_BITS_UINT64_BITS_POW_63 ((zither_bits_uint64_bits_t)(1u << 63))

// Bits with a one-line comment.
typedef uint8_t zither_bits_bits_with_one_line_comment_t;

// Bits member with one-line comment.
#define ZITHER_BITS_BITS_WITH_ONE_LINE_COMMENT_MEMBER_WITH_ONE_LINE_COMMENT \
  ((zither_bits_bits_with_one_line_comment_t)(1u << 0))

// Bits member
//     with a
//         many-line
//           comment.
#define ZITHER_BITS_BITS_WITH_ONE_LINE_COMMENT_MEMBER_WITH_MANY_LINE_COMMENT \
  ((zither_bits_bits_with_one_line_comment_t)(1u << 6))

// Bits
//
//     with a
//         many-line
//           comment.
typedef uint16_t zither_bits_bits_with_many_line_comment_t;

#define ZITHER_BITS_BITS_WITH_MANY_LINE_COMMENT_MEMBER \
  ((zither_bits_bits_with_many_line_comment_t)(1u << 0))

#define ZITHER_BITS_SEVENTY_TWO \
  ((zither_bits_uint8_bits_t)(0b1001000u))  // Uint8Bits.SIXTY_FOUR | Uint8Bits.EIGHT

#define ZITHER_BITS_SOME_BITS \
  ((zither_bits_uint16_bits_t)(0b1001000000010u))  // Uint16Bits.FIRST | Uint16Bits.NINTH |
                                                   // Uint16Bits.TWELFTH

#define ZITHER_BITS_U32_POW_0 ZITHER_BITS_UINT32_BITS_POW_0

#define ZITHER_BITS_U64_POW_63 ZITHER_BITS_UINT64_BITS_POW_63

#if defined(__cplusplus)
}
#endif

#endif  // LIB_ZITHER_BITS_C_BITS_H_

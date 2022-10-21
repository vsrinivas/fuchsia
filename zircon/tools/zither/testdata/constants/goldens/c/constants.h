// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// DO NOT EDIT. Generated from FIDL library
//   zither.constants (//zircon/tools/zither/testdata/constants/constants.test.fidl)
// by zither, a Fuchsia platform tool.

#ifndef LIB_ZITHER_CONSTANTS_C_CONSTANTS_H_
#define LIB_ZITHER_CONSTANTS_C_CONSTANTS_H_

#include <stdbool.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

#define ZITHER_CONSTANTS_UINT8_ZERO ((uint8_t)(0u))

#define ZITHER_CONSTANTS_UINT8_MAX_DEC ((uint8_t)(255u))

#define ZITHER_CONSTANTS_UINT8_MAX_HEX ((uint8_t)(0xffu))

#define ZITHER_CONSTANTS_INT8_ZERO ((int8_t)(0u))

#define ZITHER_CONSTANTS_INT8_MIN_DEC ((int8_t)(-128u))

#define ZITHER_CONSTANTS_INT8_MIN_HEX ((int8_t)(-0x80u))

#define ZITHER_CONSTANTS_INT8_MAX_DEC ((int8_t)(127u))

#define ZITHER_CONSTANTS_INT8_MAX_HEX ((int8_t)(0x7fu))

#define ZITHER_CONSTANTS_UINT16_ZERO ((uint16_t)(0u))

#define ZITHER_CONSTANTS_UINT16_MAX_DEC ((uint16_t)(65535u))

#define ZITHER_CONSTANTS_UINT16_MAX_HEX ((uint16_t)(0xffffu))

#define ZITHER_CONSTANTS_INT16_ZERO ((int16_t)(0u))

#define ZITHER_CONSTANTS_INT16_MIN_DEC ((int16_t)(-32768u))

#define ZITHER_CONSTANTS_INT16_MIN_HEX ((int16_t)(-0x8000u))

#define ZITHER_CONSTANTS_INT16_MAX_DEC ((int16_t)(32767u))

#define ZITHER_CONSTANTS_INT16_MAX_HEX ((int16_t)(0x7fffu))

#define ZITHER_CONSTANTS_UINT32_ZERO ((uint32_t)(0u))

#define ZITHER_CONSTANTS_UINT32_MAX_DEC ((uint32_t)(4294967295u))

#define ZITHER_CONSTANTS_UINT32_MAX_HEX ((uint32_t)(0xffffffffu))

#define ZITHER_CONSTANTS_INT32_ZERO ((int32_t)(0u))

#define ZITHER_CONSTANTS_INT32_MIN_DEC ((int32_t)(-2147483648u))

#define ZITHER_CONSTANTS_INT32_MIN_HEX ((int32_t)(-0x80000000u))

#define ZITHER_CONSTANTS_INT32_MAX_DEC ((int32_t)(2147483647u))

#define ZITHER_CONSTANTS_INT32_MAX_HEX ((int32_t)(0x7fffffffu))

#define ZITHER_CONSTANTS_UINT64_ZERO ((uint64_t)(0u))

#define ZITHER_CONSTANTS_UINT64_MAX_DEC ((uint64_t)(18446744073709551615u))

#define ZITHER_CONSTANTS_UINT64_MAX_HEX ((uint64_t)(0xffffffffffffffffu))

#define ZITHER_CONSTANTS_INT64_ZERO ((int64_t)(0u))

#define ZITHER_CONSTANTS_INT64_MIN_DEC ((int64_t)(-9223372036854775808u))

#define ZITHER_CONSTANTS_INT64_MIN_HEX ((int64_t)(-0x8000000000000000u))

#define ZITHER_CONSTANTS_INT64_MAX_DEC ((int64_t)(9223372036854775807u))

#define ZITHER_CONSTANTS_INT64_MAX_HEX ((int64_t)(0x7fffffffffffffffu))

#define ZITHER_CONSTANTS_FALSE ((bool)(false))

#define ZITHER_CONSTANTS_TRUE ((bool)(true))

#define ZITHER_CONSTANTS_EMPTY_STRING ""

#define ZITHER_CONSTANTS_BYTE_ZERO ((uint8_t)(0u))

#define ZITHER_CONSTANTS_BINARY_VALUE ((uint8_t)(0b10101111u))

#define ZITHER_CONSTANTS_LOWERCASE_HEX_VALUE ((uint64_t)(0x1234abcd5678ffffu))

#define ZITHER_CONSTANTS_UPPERCASE_HEX_VALUE ((uint64_t)(0x1234ABCD5678FFFFu))

#define ZITHER_CONSTANTS_LEADING_ZEROES_HEX_VALUE ((uint32_t)(0x00000011u))

#define ZITHER_CONSTANTS_LEADING_ZEROES_DEC_VALUE ((uint32_t)(0000000017u))

#define ZITHER_CONSTANTS_LEADING_ZEROES_BINARY_VALUE ((uint32_t)(0b0000000000010001u))

#define ZITHER_CONSTANTS_BITWISE_OR_VALUE ((uint8_t)(15u))  // 0b1000 | 0b0100 | 0b0010 | 0b0001

#define ZITHER_CONSTANTS_NONEMPTY_STRING "this is a constant"

#define ZITHER_CONSTANTS_DEFINITION_FROM_ANOTHER_CONSTANT ZITHER_CONSTANTS_NONEMPTY_STRING

#define ZITHER_CONSTANTS_BITWISE_OR_OF_OTHER_CONSTANTS \
  ((uint8_t)(175u))  // BINARY_VALUE | BITWISE_OR_VALUE | 0b1 | UINT8_ZERO

#define ZITHER_CONSTANTS_EXPERIMENTAL_UCHAR ((char)(97u))

#define ZITHER_CONSTANTS_EXPERIMENTAL_USIZE ((size_t)(100u))

#define ZITHER_CONSTANTS_EXPERIMENTAL_UINTPTR ((uintptr_t)(0x1234abcd5678ffffu))

// Constant with a one-line comment.
#define ZITHER_CONSTANTS_CONSTANT_ONE_LINE_COMMENT ((bool)(true))

// Constant
//
//     with
//         a
//           many-line
//             comment.
#define ZITHER_CONSTANTS_CONSTANT_MANY_LINE_COMMENT ""

#if defined(__cplusplus)
}
#endif

#endif  // LIB_ZITHER_CONSTANTS_C_CONSTANTS_H_

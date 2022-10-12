// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// DO NOT EDIT. Generated from FIDL library
//   zither.enums (//zircon/tools/zither/testdata/enums/enums.test.fidl)
// by zither, a Fuchsia platform tool.

#ifndef LIB_ZITHER_ENUMS_C_ENUMS_H_
#define LIB_ZITHER_ENUMS_C_ENUMS_H_

#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef uint8_t zither_enums_color_t;

#define ZITHER_ENUMS_COLOR_RED ((zither_enums_color_t)(0u))
#define ZITHER_ENUMS_COLOR_ORANGE ((zither_enums_color_t)(1u))
#define ZITHER_ENUMS_COLOR_YELLOW ((zither_enums_color_t)(2u))
#define ZITHER_ENUMS_COLOR_GREEN ((zither_enums_color_t)(3u))
#define ZITHER_ENUMS_COLOR_BLUE ((zither_enums_color_t)(4u))
#define ZITHER_ENUMS_COLOR_INDIGO ((zither_enums_color_t)(5u))
#define ZITHER_ENUMS_COLOR_VIOLET ((zither_enums_color_t)(6u))

typedef uint8_t zither_enums_uint8_limits_t;

#define ZITHER_ENUMS_UINT8_LIMITS_MIN ((zither_enums_uint8_limits_t)(0u))
#define ZITHER_ENUMS_UINT8_LIMITS_MAX ((zither_enums_uint8_limits_t)(0b11111111u))

typedef uint16_t zither_enums_uint16_limits_t;

#define ZITHER_ENUMS_UINT16_LIMITS_MIN ((zither_enums_uint16_limits_t)(0u))
#define ZITHER_ENUMS_UINT16_LIMITS_MAX ((zither_enums_uint16_limits_t)(0xffffu))

typedef uint32_t zither_enums_uint32_limits_t;

#define ZITHER_ENUMS_UINT32_LIMITS_MIN ((zither_enums_uint32_limits_t)(0u))
#define ZITHER_ENUMS_UINT32_LIMITS_MAX ((zither_enums_uint32_limits_t)(0xffffffffu))

typedef uint64_t zither_enums_uint64_limits_t;

#define ZITHER_ENUMS_UINT64_LIMITS_MIN ((zither_enums_uint64_limits_t)(0u))
#define ZITHER_ENUMS_UINT64_LIMITS_MAX ((zither_enums_uint64_limits_t)(0xffffffffffffffffu))

typedef int8_t zither_enums_int8_limits_t;

#define ZITHER_ENUMS_INT8_LIMITS_MIN ((zither_enums_int8_limits_t)(-0x80u))
#define ZITHER_ENUMS_INT8_LIMITS_MAX ((zither_enums_int8_limits_t)(0x7fu))

typedef int16_t zither_enums_int16_limits_t;

#define ZITHER_ENUMS_INT16_LIMITS_MIN ((zither_enums_int16_limits_t)(-0x8000u))
#define ZITHER_ENUMS_INT16_LIMITS_MAX ((zither_enums_int16_limits_t)(0x7fffu))

typedef int32_t zither_enums_int32_limits_t;

#define ZITHER_ENUMS_INT32_LIMITS_MIN ((zither_enums_int32_limits_t)(-0x80000000u))
#define ZITHER_ENUMS_INT32_LIMITS_MAX ((zither_enums_int32_limits_t)(0x7fffffffu))

typedef int64_t zither_enums_int64_limits_t;

#define ZITHER_ENUMS_INT64_LIMITS_MIN ((zither_enums_int64_limits_t)(-0x8000000000000000u))
#define ZITHER_ENUMS_INT64_LIMITS_MAX ((zither_enums_int64_limits_t)(0x7fffffffffffffffu))

// Enum with a one-line comment.
typedef uint8_t zither_enums_enum_with_one_line_comment_t;

// Enum member with one-line comment.
#define ZITHER_ENUMS_ENUM_WITH_ONE_LINE_COMMENT_MEMBER_WITH_ONE_LINE_COMMENT \
  ((zither_enums_enum_with_one_line_comment_t)(0u))

// Enum member
//     with a
//         many-line
//           comment.
#define ZITHER_ENUMS_ENUM_WITH_ONE_LINE_COMMENT_MEMBER_WITH_MANY_LINE_COMMENT \
  ((zither_enums_enum_with_one_line_comment_t)(1u))

// Enum
//
//     with a
//         many-line
//           comment.
typedef uint16_t zither_enums_enum_with_many_line_comment_t;

#define ZITHER_ENUMS_ENUM_WITH_MANY_LINE_COMMENT_MEMBER \
  ((zither_enums_enum_with_many_line_comment_t)(0u))

#define ZITHER_ENUMS_RED ZITHER_ENUMS_COLOR_RED

#define ZITHER_ENUMS_UINT8_MIN ZITHER_ENUMS_UINT8_LIMITS_MIN

#define ZITHER_ENUMS_UINT8_MAX ZITHER_ENUMS_UINT8_LIMITS_MAX

#define ZITHER_ENUMS_UINT16_MIN ZITHER_ENUMS_UINT16_LIMITS_MIN

#define ZITHER_ENUMS_UINT16_MAX ZITHER_ENUMS_UINT16_LIMITS_MAX

#define ZITHER_ENUMS_UINT32_MIN ZITHER_ENUMS_UINT32_LIMITS_MIN

#define ZITHER_ENUMS_UINT32_MAX ZITHER_ENUMS_UINT32_LIMITS_MAX

#define ZITHER_ENUMS_UINT64_MIN ZITHER_ENUMS_UINT64_LIMITS_MIN

#define ZITHER_ENUMS_UINT64_MAX ZITHER_ENUMS_UINT64_LIMITS_MAX

#define ZITHER_ENUMS_INT8_MIN ZITHER_ENUMS_INT8_LIMITS_MIN

#define ZITHER_ENUMS_INT8_MAX ZITHER_ENUMS_INT8_LIMITS_MAX

#define ZITHER_ENUMS_INT16_MIN ZITHER_ENUMS_INT16_LIMITS_MIN

#define ZITHER_ENUMS_INT16_MAX ZITHER_ENUMS_INT16_LIMITS_MAX

#define ZITHER_ENUMS_INT32_MIN ZITHER_ENUMS_INT32_LIMITS_MIN

#define ZITHER_ENUMS_INT32_MAX ZITHER_ENUMS_INT32_LIMITS_MAX

#define ZITHER_ENUMS_INT64_MIN ZITHER_ENUMS_INT64_LIMITS_MIN

#define ZITHER_ENUMS_INT64_MAX ZITHER_ENUMS_INT64_LIMITS_MAX

#if defined(__cplusplus)
}
#endif

#endif  // LIB_ZITHER_ENUMS_C_ENUMS_H_

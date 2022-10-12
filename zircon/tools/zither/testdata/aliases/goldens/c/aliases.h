// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// DO NOT EDIT. Generated from FIDL library
//   zither.aliases (//zircon/tools/zither/testdata/aliases/aliases.test.fidl)
// by zither, a Fuchsia platform tool.

#ifndef LIB_ZITHER_ALIASES_C_ALIASES_H_
#define LIB_ZITHER_ALIASES_C_ALIASES_H_

#include <stdbool.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef bool zither_aliases_bool_alias_t;

typedef int8_t zither_aliases_int8_alias_t;

typedef int16_t zither_aliases_int16_alias_t;

typedef int32_t zither_aliases_int32_alias_t;

typedef int64_t zither_aliases_int64_alias_t;

typedef uint8_t zither_aliases_uint8_alias_t;

typedef uint16_t zither_aliases_uint16_alias_t;

typedef uint32_t zither_aliases_uint32_alias_t;

typedef uint64_t zither_aliases_uint64_alias_t;

// TODO(fxbug.dev/105758): The IR currently does not propagate enough
// information for bindings to express this type as an alias.
#define ZITHER_ALIASES_CONST_FROM_ALIAS ((uint8_t)(0xffu))

typedef int16_t zither_aliases_enum_t;

#define ZITHER_ALIASES_ENUM_MEMBER ((zither_aliases_enum_t)(0u))

typedef zither_aliases_enum_t zither_aliases_enum_alias_t;

typedef uint16_t zither_aliases_bits_t;

#define ZITHER_ALIASES_BITS_ONE ((zither_aliases_bits_t)(1u))

typedef zither_aliases_bits_t zither_aliases_bits_alias_t;

typedef struct {
  uint64_t x;
  uint64_t y;
} zither_aliases_struct_t;

typedef zither_aliases_struct_t zither_aliases_struct_alias_t;

typedef uint32_t zither_aliases_array_alias_t[4];

typedef zither_aliases_struct_t zither_aliases_nested_array_alias_t[8][4];

// Alias with a one-line comment.
typedef bool zither_aliases_alias_with_one_line_comment_t;

// Alias
//     with
//         a
//           many-line
//             comment.
typedef uint8_t zither_aliases_alias_with_many_line_comment_t;

#if defined(__cplusplus)
}
#endif

#endif  // LIB_ZITHER_ALIASES_C_ALIASES_H_

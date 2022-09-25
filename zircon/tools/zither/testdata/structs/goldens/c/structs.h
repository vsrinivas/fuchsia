// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// DO NOT EDIT.
// Generated from FIDL library `zither.structs` by zither, a Fuchsia platform tool.

#ifndef LIB_ZITHER_STRUCTS_C_STRUCTS_H_
#define LIB_ZITHER_STRUCTS_C_STRUCTS_H_

#include <stdbool.h>
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
} zither_structs_empty_t;

typedef struct {
  uint8_t value;
} zither_structs_singleton_t;

typedef struct {
  zither_structs_singleton_t first;
  zither_structs_singleton_t second;
} zither_structs_doubtleton_t;

typedef struct {
  int64_t i64;
  uint64_t u64;
  int32_t i32;
  uint32_t u32;
  int16_t i16;
  uint16_t u16;
  int8_t i8;
  uint8_t u8;
  bool b;
} zither_structs_primitive_members_t;

typedef struct {
  uint8_t u8s[10];
  zither_structs_singleton_t singletons[6];
  uint8_t nested_arrays1[10][20];
  int8_t nested_arrays2[1][2][3];
} zither_structs_array_members_t;

// Struct with a one-line comment.
typedef struct {
  // Struct member with one-line comment.
  uint32_t member_with_one_line_comment;

  // Struct member
  //     with a
  //         many-line
  //           comment.
  bool member_with_many_line_comment;
} zither_structs_struct_with_one_line_comment_t;

// Struct
//
//     with a
//         many-line
//           comment.
typedef struct {
  uint16_t member;
} zither_structs_struct_with_many_line_comment_t;

#if defined(__cplusplus)
}
#endif

#endif  // LIB_ZITHER_STRUCTS_C_STRUCTS_H_

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_OPCODES_H_
#define SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_OPCODES_H_

//
// WARNING: This header will be removed in the future!
//
// List of styling opcode definitions. These are not meant to be part of
// the public API, but are required to be exposed for unit-testing at the
// moment since there is no better way to encapsulate / check them for
// now. These values may change at any time, or this header may even
// disappear, so do not rely on it in client code!
//

//
// clang-format off
//

#define SPN_STYLING_OPCODE_NOOP                        0

#define SPN_STYLING_OPCODE_COVER_NONZERO               1
#define SPN_STYLING_OPCODE_COVER_EVENODD               2
#define SPN_STYLING_OPCODE_COVER_ACCUMULATE            3
#define SPN_STYLING_OPCODE_COVER_MASK                  4

#define SPN_STYLING_OPCODE_COVER_WIP_ZERO              5
#define SPN_STYLING_OPCODE_COVER_ACC_ZERO              6
#define SPN_STYLING_OPCODE_COVER_MASK_ZERO             7
#define SPN_STYLING_OPCODE_COVER_MASK_ONE              8
#define SPN_STYLING_OPCODE_COVER_MASK_INVERT           9

#define SPN_STYLING_OPCODE_COLOR_FILL_SOLID            10
#define SPN_STYLING_OPCODE_COLOR_FILL_GRADIENT_LINEAR  11

#define SPN_STYLING_OPCODE_COLOR_WIP_ZERO              12
#define SPN_STYLING_OPCODE_COLOR_ACC_ZERO              13

#define SPN_STYLING_OPCODE_BLEND_OVER                  14
#define SPN_STYLING_OPCODE_BLEND_PLUS                  15
#define SPN_STYLING_OPCODE_BLEND_MULTIPLY              16
#define SPN_STYLING_OPCODE_BLEND_KNOCKOUT              17

#define SPN_STYLING_OPCODE_COVER_WIP_MOVE_TO_MASK      18
#define SPN_STYLING_OPCODE_COVER_ACC_MOVE_TO_MASK      19

#define SPN_STYLING_OPCODE_COLOR_ACC_OVER_BACKGROUND   20
#define SPN_STYLING_OPCODE_COLOR_ACC_STORE_TO_SURFACE  21
#define SPN_STYLING_OPCODE_COLOR_ACC_TEST_OPACITY      22

#define SPN_STYLING_OPCODE_COLOR_ILL_ZERO              23
#define SPN_STYLING_OPCODE_COLOR_ILL_COPY_ACC          24
#define SPN_STYLING_OPCODE_COLOR_ACC_MULTIPLY_ILL      25

#define SPN_STYLING_OPCODE_COUNT                       26

//
// clang-format on
//

#endif  // SRC_GRAPHICS_LIB_COMPUTE_SPINEL_INCLUDE_SPINEL_SPINEL_OPCODES_H_

// Copyright 2022 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <lib/arch/asm.h>

#include "user-copy-asm.h"

// Define instruction set for copy to user.
.macro ld_byte reg0, base_register, offset=0
  ldrb \reg0, [\base_register, #\offset]
.endm

.macro ld_reg reg0, base_register, offset=0
  ldr \reg0, [\base_register, #\offset]
.endm

.macro ld_pair reg0, reg1, base_register, offset=0
  ldp \reg0, \reg1, [\base_register, #\offset]
.endm

.macro ld_pair_wb reg0, reg1, base_register, offset=0
  ldp \reg0, \reg1, [\base_register, #\offset]!
.endm

.macro st_byte reg0, base_register, offset=0
  sttrb \reg0, [\base_register, #\offset]
.endm

.macro st_reg reg0, base_register, offset=0
  sttr \reg0, [\base_register, #\offset]
.endm

.macro st_pair reg0, reg1, base_register, offset=0
  sttr \reg0, [\base_register, #\offset]
  sttr \reg1, [\base_register, #\offset + 8]
.endm

.macro st_pair_wb reg0, reg1, base_register, offset=0
  st_pair \reg0, \reg1, \base_register, \offset
  add \base_register, \base_register, #\offset
.endm

// Arm64UserCopyRet _arm64_user_copy_to_user(void *dst, const void *src, size_t len, uint64_t *fault_return, uint64_t fault_return_mask)
.function _arm64_user_copy_to_user, global, align=64
  arm64_user_copy_prologue arm64_user_copy_to_user
  arm64_user_copy_body arm64_user_copy_to_user
  arm64_user_copy_epilogue arm64_user_copy_to_user
.end_function


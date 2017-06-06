// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#ifndef __has_feature
#define __has_feature(x) 0
#endif

#define ALIAS_ENTRY(name) \
    .globl name; \
    .type name, %function; \
name:

#define ALIAS_END(name) \
    .size name, . - name

#define ENTRY(name) \
    ALIAS_ENTRY(name) \
    .cfi_startproc

#define END(name) \
    .cfi_endproc; \
    ALIAS_END(name)

#ifdef __aarch64__
.macro push_regs reg1, reg2
    stp \reg1, \reg2, [sp, #-16]!
    .cfi_adjust_cfa_offset 16
    .cfi_rel_offset \reg1, 0
    .cfi_rel_offset \reg2, 8
.endm
.macro pop_regs reg1, reg2
    ldp \reg1, \reg2, [sp], #16
    .cfi_adjust_cfa_offset -16
    .cfi_same_value \reg1
    .cfi_same_value \reg2
.endm
#endif

#ifdef __x86_64__
.macro push_reg reg
    push \reg
    .cfi_adjust_cfa_offset 8
    .cfi_rel_offset \reg, 0
.endm
.macro pop_reg reg
    pop \reg
    .cfi_adjust_cfa_offset -8
    .cfi_same_value \reg
.endm
#endif

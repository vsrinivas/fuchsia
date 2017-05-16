// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

.macro magenta_syscall num, name, caller
    mov x16, #\num
    svc #0x0
// This symbol at the return address identifies this as an approved call site.
    .hidden CODE_SYSRET_\name\()_VIA_\caller
CODE_SYSRET_\name\()_VIA_\caller\():
.endm

// CFI aware push and pop macros.
// SP must always be aligned to 16, so never push just one register.
.macro push_regpair reg0, reg1
    stp \reg0, \reg1, [sp, #-16]!
    .cfi_adjust_cfa_offset 16
    .cfi_rel_offset \reg0, 0
    .cfi_rel_offset \reg1, 8
.endm
.macro pop_regpair reg0, reg1
    ldp \reg0, \reg1, [sp], #16
    .cfi_adjust_cfa_offset -16
    .cfi_same_value \reg0
    .cfi_same_value \reg1
.endm

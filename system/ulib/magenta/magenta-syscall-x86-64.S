// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

.macro magenta_syscall num, name, caller
    mov $\num, %eax
    syscall
// This symbol at the return address identifies this as an approved call site.
    .hidden CODE_SYSRET_\name\()_VIA_\caller
CODE_SYSRET_\name\()_VIA_\caller\():
.endm

// CFI aware push and pop macros.
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

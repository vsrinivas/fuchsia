// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/* define and implement the magenta syscall wrappers for x86-64 */

.text

.cfi_sections .eh_frame, .debug_frame

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

#define MAGENTA_SYSCALL_MAGIC 0x00ff00ff00000000

.macro _syscall nargs, name, n
.globl _\name
.type _\name,STT_FUNC
_\name:
    .cfi_startproc
    .cfi_same_value %r10
    .cfi_same_value %r11
    .cfi_same_value %r12
    .cfi_same_value %r13
.if \nargs <= 6
    push_reg %r10
    push_reg %r11
    mov      %rcx, %r10
    movabs   $(MAGENTA_SYSCALL_MAGIC | \n), %rax
    syscall
    pop_reg  %r11
    pop_reg  %r10
    ret
.endif
.if \nargs == 7
    push_reg %r10
    push_reg %r11
    push_reg %r12
    mov      0x20(%rsp), %r12
    mov      %rcx, %r10
    movabs   $(MAGENTA_SYSCALL_MAGIC | \n), %rax
    syscall
    pop_reg  %r12
    pop_reg  %r11
    pop_reg  %r10
    ret
.endif
.if \nargs == 8
    push_reg %r10
    push_reg %r11
    push_reg %r12
    push_reg %r13
    mov      0x28(%rsp), %r12
    mov      0x30(%rsp), %r13
    mov      %rcx, %r10
    movabs   $(MAGENTA_SYSCALL_MAGIC | \n), %rax
    syscall
    pop_reg  %r13
    pop_reg  %r12
    pop_reg  %r11
    pop_reg  %r10
    ret
.endif
    .cfi_endproc
.size _\name, . - _\name
.weak \name
.type \name,STT_FUNC
\name = _\name
.size \name, . - _\name
.endm

#define MAGENTA_SYSCALL_DEF(nargs64, nargs32, n, ret, name, args...) _syscall nargs64, mx_##name, n

#include <magenta/syscalls.inc>

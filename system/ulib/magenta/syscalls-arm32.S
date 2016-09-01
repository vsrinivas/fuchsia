// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/* define and implement the magenta syscall wrappers for 32bit arm */

.text

#define MAGENTA_SYSCALL_MAGIC 0xf0f0

// EABI unwind directives cause an implicit reference to this symbol,
// though it's never actually used for anything.  Define it locally
// just to resolve that reference.
__aeabi_unwind_cpp_pr0 = 0xdeadbeef

.cfi_sections .debug_frame

.macro push_regs reglist:vararg
    push {\reglist}
    .save {\reglist}
    push_regs_cfi \reglist
.endm
.macro push_regs_cfi reg, reglist:vararg
    .cfi_adjust_cfa_offset 4
    .cfi_rel_offset \reg, 0
    .ifnb \reglist
    push_regs_cfi \reglist
    .endif
.endm
.macro pop_regs reglist:vararg
    pop {\reglist}
    pop_regs_cfi \reglist
.endm
.macro pop_regs_cfi reg, reglist:vararg
    .cfi_adjust_cfa_offset -4
    .cfi_same_value \reg
    .ifnb \reglist
    pop_regs_cfi \reglist
    .endif
.endm

.macro syscall nargs, name, n
.globl _\name
.type _\name,STT_FUNC
_\name:
    .fnstart
    .cfi_startproc
    .cfi_same_value r4
    .cfi_same_value r5
    .cfi_same_value r6
    .cfi_same_value r7
.if \nargs < 5
    movw   r12, #\n
    movt   r12, #MAGENTA_SYSCALL_MAGIC
    svc    #0xff00ff
    mov    r12, #0
    bx     lr
.endif
.if \nargs == 5
    push_regs r4, r5
    ldr    r4, [sp, #8]
    movw   r12, #\n
    movt   r12, #MAGENTA_SYSCALL_MAGIC
    svc    #0xff00ff
    mov    r12, #0
    pop_regs r4, r5
    bx     lr
.endif
.if \nargs == 6
    push_regs r4, r5
    ldrd   r4, r5, [sp, #8]
    movw   r12, #\n
    movt   r12, #MAGENTA_SYSCALL_MAGIC
    svc    #0xff00ff
    mov    r12, #0
    pop_regs r4, r5
    bx     lr
.endif
.if \nargs == 7
    push_regs r4, r5, r6, r7
    ldrd   r4, r5, [sp, #16]
    ldr    r6, [sp, #24]
    movw   r12, #\n
    movt   r12, #MAGENTA_SYSCALL_MAGIC
    svc    #0xff00ff
    mov    r12, #0
    pop_regs r4, r5, r6, r7
    bx     lr
.endif
.if \nargs == 8
    push_regs r4, r5, r6, r7
    ldrd   r4, r5, [sp, #16]
    ldrd   r6, r7, [sp, #24]
    movw   r12, #\n
    movt   r12, #MAGENTA_SYSCALL_MAGIC
    svc    #0xff00ff
    mov    r12, #0
    pop_regs r4, r5, r6, r7
    bx     lr
.endif
    .cfi_endproc
    .fnend
.size _\name, . - _\name
.weak \name
.type \name,STT_FUNC
\name = _\name
.size \name, . - _\name
.endm

#define MAGENTA_SYSCALL_DEF(nargs64, nargs32, n, ret, name, args...) syscall nargs32, mx_##name, n

#include <magenta/syscalls.inc>

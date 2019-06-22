// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/tls.h>

.text
.balign 16
.globl _start
.hidden _start
.type _start, %function
_start:
    .cfi_startproc simple

// The first argument (x0, %rdi) is a zx_handle_t.  It stays where it is.
// The second argument (x1, %rsi) is the hermetic::Tcb pointer.  We
// point the thread pointer at the right spot in the Tcb struct.

#ifdef __aarch64__

    .cfi_return_column x30
    .cfi_undefined x30
    .cfi_def_cfa sp, 8*8

    // The thread pointer should point just past the TCB.
    add x1, x1, #-ZX_TLS_STACK_GUARD_OFFSET
    msr tpidr_el0, x1

    // Pop x18 (shadow call stack pointer) and 7 argument registers off the
    // stack, plus a phantom pop to maintain SP alignment.  The first
    // argument register (x0) already holds the zx_handle_t from entry.  To
    // be extra hermetic, scrub the memory after loading the registers.
    ldp x18, x1, [sp]
    stp xzr, xzr, [sp], #16
    .cfi_adjust_cfa_offset -16
    ldp x2, x3, [sp]
    stp xzr, xzr, [sp], #16
    .cfi_adjust_cfa_offset -16
    ldp x4, x5, [sp]
    stp xzr, xzr, [sp], #16
    .cfi_adjust_cfa_offset -16
    ldp x6, x7, [sp]
    stp xzr, xzr, [sp], #16
    .cfi_adjust_cfa_offset -16

    // Now SP is aligned to 16 and points to any stack arguments, or is all
    // the way at the top if there weren't more than 8 arguments.  So the C
    // ABI has been met.
    bl EngineMain
    brk #0

#elif defined(__x86_64__)

#define FRAME_SIZE (6 * 8) // 5 registers plus alignment padding

    .cfi_return_column %rip
    .cfi_undefined %rip
    .cfi_def_cfa %rsp, FRAME_SIZE

    // Point the thread pointer at the TCB.
    wrfsbase %rsi

    // Pop the argument registers and scrub the memory where they were.
    xor %eax, %eax
    mov 0*8(%rsp), %rsi
    mov %rax, 0*8(%rsp)
    mov 1*8(%rsp), %rdx
    mov %rax, 1*8(%rsp)
    mov 2*8(%rsp), %rcx
    mov %rax, 2*8(%rsp)
    mov 3*8(%rsp), %r8
    mov %rax, 3*8(%rsp)
    mov 4*8(%rsp), %r9
    mov %rax, 4*8(%rsp)

    add $FRAME_SIZE, %rsp
    .cfi_adjust_cfa_offset -FRAME_SIZE

    // Now SP is aligned to 16 and points to any stack arguments, or is all
    // the way at the top if there weren't more than 6 arguments.  So the C
    // ABI has been met.
    call EngineMain
    ud2

#else
# error "What machine?"
#endif

    .cfi_endproc
.size _start, . - _start

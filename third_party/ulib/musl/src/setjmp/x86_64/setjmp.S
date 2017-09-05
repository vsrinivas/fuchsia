// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "asm.h"
#include "setjmp_impl.h"
#include <magenta/tls.h>

ENTRY(setjmp)

    // Copy the values that need mangling into temporaries.
    mov (%rsp), %rax                            // PC
    mov %rsp, %rcx                              // SP
    mov %rbp, %rdx                              // FP
    mov %fs:MX_TLS_UNSAFE_SP_OFFSET, %rsi       // Unsafe SP

    // Store all the vanilla callee-saves registers.
    mov %rbx, 8*JB_RBX(%rdi)
    mov %r12, 8*JB_R12(%rdi)
    mov %r13, 8*JB_R13(%rdi)
    mov %r14, 8*JB_R14(%rdi)
    mov %r15, 8*JB_R15(%rdi)

    // Mangle each temporary.
    xor __setjmp_manglers+8*JB_PC(%rip), %rax
    xor __setjmp_manglers+8*JB_SP(%rip), %rcx
    xor __setjmp_manglers+8*JB_FP(%rip), %rdx
    xor __setjmp_manglers+8*JB_USP(%rip), %rsi

    // Store the mangled values.
    mov %rax, 8*JB_PC(%rdi)
    mov %rcx, 8*JB_SP(%rdi)
    mov %rdx, 8*JB_FP(%rdi)
    mov %rsi, 8*JB_USP(%rdi)

    // %rax = 0
    xor %eax, %eax

    ret

END(setjmp)

ALIAS(setjmp, _setjmp)
WEAK_ALIAS(setjmp, sigsetjmp)

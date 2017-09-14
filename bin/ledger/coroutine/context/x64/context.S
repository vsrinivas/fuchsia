// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/tls.h>

#include "apps/ledger/src/coroutine/context/x64/context.h"

#define FUNCTION(x) .global x; .type x,%function; x:

// GetContext
// Captures the state of the CPU in the passed struct.
// The saved registers are:
// - All callee saved registers.
// - SP, Unsafe SP, LR and ARG0.
FUNCTION(_ZN7context10GetContextEPNS_15InternalContextE)
    movq %rbx,    RBX_O(%rdi)
    movq %rbp,    RBP_O(%rdi)
    movq %r12,    R12_O(%rdi)
    movq %r13,    R13_O(%rdi)
    movq %r14,    R14_O(%rdi)
    movq %r15,    R15_O(%rdi)
    movq %rdi,    RDI_O(%rdi)

    movq %fs:ZX_TLS_UNSAFE_SP_OFFSET, %rax
    movq %rax, UNSAFE_SP_O(%rdi)

    # Skip return address.
    leaq 8(%rsp), %rax
    movq %rax,    RSP_O(%rdi)
    # Store return value in rip.
    movq (%rsp),  %rax
    movq %rax,    RIP_O(%rdi)

    // Returns true for initial call.
    xor %rax,     %rax
    dec %rax
    ret

// SetContext
// Restores the state of the CPU from the passed struct.
// The restored registers are:
// - All callee saved registers.
// - SP, Unsafe SP, LR and ARG0.
FUNCTION(_ZN7context10SetContextEPNS_15InternalContextE)
    // The sanitizer runtime wants to be informed of non-local exits.
    // Call __asan_handle_no_return() before doing the actual longjmp.
#if __has_feature(address_sanitizer)
    // Save our incoming argument register on the stack around calling
    // __asan_handle_no_return.  The incoming stack is misaligned by one
    // word, so we need to move the stack by an odd number of words.
    push %rdi
    call __asan_handle_no_return@PLT
    pop %rdi
#endif

    movq RBX_O(%rdi), %rbx
    movq RBP_O(%rdi), %rbp
    movq R12_O(%rdi), %r12
    movq R13_O(%rdi), %r13
    movq R14_O(%rdi), %r14
    movq R15_O(%rdi), %r15
    movq RSP_O(%rdi), %rsp

    movq UNSAFE_SP_O(%rdi), %rax
    movq %rax, %fs:ZX_TLS_UNSAFE_SP_OFFSET

    # Returns 0.
    xor %rax,         %rax

    # Get the return address.
    movq RIP_O(%rdi), %rcx

    # Set RDI at the end.
    movq RDI_O(%rdi), %rdi

    # Jump directly to the return address.
    jmp *%rcx

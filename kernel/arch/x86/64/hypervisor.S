// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>

.text

/* int vmx_host_save(VmxHostState* host_state) */
FUNCTION(vmx_host_save)
    // Store the return address.
    // We do this first to adjust the %rsp we store.
    pop 56(%rdi)

    // Store the callee save registers.
    mov %rsp, 0(%rdi)
    mov %rbp, 8(%rdi)
    mov %rbx, 16(%rdi)
    mov %r12, 24(%rdi)
    mov %r13, 32(%rdi)
    mov %r14, 40(%rdi)
    mov %r15, 48(%rdi)

    // Return 0.
    mov $0, %eax
    jmp *56(%rdi)

/* This is effectively the second-half of vmx_host_save. When we return from a
 * VM exit, host_state argument is stored in %rsp. We use this to restore the
 * stack and registers to the state they were in when vmx_host_save was called.
 */
FUNCTION(vmx_host_load)
    // Load host_state from %rsp.
    mov %rsp, %rdi

    // Load the callee save registers.
    mov 0(%rdi), %rsp
    mov 8(%rdi), %rbp
    mov 16(%rdi), %rbx
    mov 24(%rdi), %r12
    mov 32(%rdi), %r13
    mov 40(%rdi), %r14
    mov 48(%rdi), %r15

    // Return 1, using the return address of vmx_host_save.
    mov $1, %eax
    jmp *56(%rdi)

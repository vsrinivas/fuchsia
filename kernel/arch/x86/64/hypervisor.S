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
    popq 56(%rdi)

    // Store the callee save registers.
    movq %rsp, 0(%rdi)
    movq %rbp, 8(%rdi)
    movq %rbx, 16(%rdi)
    movq %r12, 24(%rdi)
    movq %r13, 32(%rdi)
    movq %r14, 40(%rdi)
    movq %r15, 48(%rdi)

    // Return 0.
    pushq 56(%rdi)
    movl $0, %eax
    retq

/* This is effectively the second-half of vmx_host_save. When we return from a
 * VM exit, host_state argument is stored in %rsp. We use this to restore the
 * stack and registers to the state they were in when vmx_host_save was called.
 */
FUNCTION(vmx_host_load)
    // Load host_state from %rsp.
    movq %rsp, %rdi

    // Load the callee save registers.
    movq 0(%rdi), %rsp
    movq 8(%rdi), %rbp
    movq 16(%rdi), %rbx
    movq 24(%rdi), %r12
    movq 32(%rdi), %r13
    movq 40(%rdi), %r14
    movq 48(%rdi), %r15

    // Return 1, using the return address of vmx_host_save.
    pushq 56(%rdi)
    movl $1, %eax
    retq

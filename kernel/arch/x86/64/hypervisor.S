// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>

#include <arch/x86/hypervisor_host.h>
#include <magenta/errors.h>

.text

/* int vmx_launch(VmxHostState* host_state) */
FUNCTION(vmx_launch)
    // Store the return address.
    // We do this first to adjust the %rsp we store.
    popq VHS_RIP(%rdi)

    // Store the callee save registers.
    mov %rsp, VHS_RSP(%rdi)
    mov %rbp, VHS_RBP(%rdi)
    mov %rbx, VHS_RBX(%rdi)
    mov %r12, VHS_R12(%rdi)
    mov %r13, VHS_R13(%rdi)
    mov %r14, VHS_R14(%rdi)
    mov %r15, VHS_R15(%rdi)

    // Store the processor flags.
    pushfq
    popq VHS_RFLAGS(%rdi)

    // Launch the guest.
    vmlaunch

    // Return ERR_INTERNAL, as this will only execute if vmlaunch failed.
    mov $ERR_INTERNAL, %eax
    jmp *VHS_RIP(%rdi)

/* This is effectively the second-half of vmx_launch. When we return from a
 * VM exit, host_state argument is stored in %rsp. We use this to restore the
 * stack and registers to the state they were in when vmx_launch was called.
 */
FUNCTION(vmx_exit_entry)
    // Reload the global descriptor table in order to restore its limit. VMX
    // always restores it with a limit of 0xffff, which is too large.
    lgdt _gdtr

    // Load host_state from %rsp.
    mov %rsp, %rdi

    // Load the callee save registers.
    mov VHS_RSP(%rdi), %rsp
    mov VHS_RBP(%rdi), %rbp
    mov VHS_RBX(%rdi), %rbx
    mov VHS_R12(%rdi), %r12
    mov VHS_R13(%rdi), %r13
    mov VHS_R14(%rdi), %r14
    mov VHS_R15(%rdi), %r15

    // Load the processor flags.
    pushq VHS_RFLAGS(%rdi)
    popfq

    // Set up the return address.
    pushq VHS_RIP(%rdi)

    // Call vmx_exit(host_state).
    sub $8, %rsp
    call vmx_exit
    add $8, %rsp

    // Return 0, using the return address of vmx_launch pushed above.
    mov $0, %eax
    ret

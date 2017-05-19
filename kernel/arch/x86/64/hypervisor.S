// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>

#include <arch/x86/hypervisor_state.h>
#include <magenta/errors.h>

.text

/* status_t vmx_enter(VmxState* vmx_state) */
FUNCTION(vmx_enter)
    // Store the return address.
    // We do this first to adjust the RSP we store.
    popq HS_RIP(%rdi)

    // Store the callee save registers.
    mov %rbx, HS_RBX(%rdi)
    mov %rsp, HS_RSP(%rdi)
    mov %rbp, HS_RBP(%rdi)
    mov %r12, HS_R12(%rdi)
    mov %r13, HS_R13(%rdi)
    mov %r14, HS_R14(%rdi)
    mov %r15, HS_R15(%rdi)

    // Store the processor flags.
    pushfq
    popq HS_RFLAGS(%rdi)

    // We are going to trample RDI, so move it to RSP. This also conveniently
    // mirrors the exit code.
    mov %rdi, %rsp

    // Load the guest CR2.
    mov GS_CR2(%rsp), %rax
    mov %rax, %cr2

    // Load the guest registers not covered by the VMCS.
    mov GS_RAX(%rsp), %rax
    mov GS_RCX(%rsp), %rcx
    mov GS_RDX(%rsp), %rdx
    mov GS_RBX(%rsp), %rbx
    mov GS_RBP(%rsp), %rbp
    mov GS_RSI(%rsp), %rsi
    mov GS_RDI(%rsp), %rdi
    mov GS_R8(%rsp), %r8
    mov GS_R9(%rsp), %r9
    mov GS_R10(%rsp), %r10
    mov GS_R11(%rsp), %r11
    mov GS_R12(%rsp), %r12
    mov GS_R13(%rsp), %r13
    mov GS_R14(%rsp), %r14
    mov GS_R15(%rsp), %r15

    // If we are resuming, jump to resume.
    testb $1, VS_RESUME(%rsp)
    jnz resume

    // Launch the guest.
    vmlaunch
    jmp failure

resume:
    // Resume the guest.
    vmresume

failure:
    // We will only be here if vmlaunch or vmresume failed.
    // Restore host RDI and RSP.
    mov %rsp, %rdi
    mov HS_RSP(%rdi), %rsp

    // Set up the return address.
    pushq HS_RIP(%rdi)

    // Return ERR_INTERNAL.
    mov $ERR_INTERNAL, %eax
    ret

/* This is effectively the second-half of vmx_enter. When we return from a
 * VM exit, vmx_state argument is stored in RSP. We use this to restore the
 * stack and registers to the state they were in when vmx_enter was called.
 */
FUNCTION(vmx_exit_entry)
    // Reload the global descriptor table in order to restore its limit. VMX
    // always restores it with a limit of 0xffff, which is too large.
    lgdt _gdtr

    // Store the guest registers not covered by the VMCS. At this point,
    // vmx_state is in RSP.
    mov %rax, GS_RAX(%rsp)
    mov %rcx, GS_RCX(%rsp)
    mov %rdx, GS_RDX(%rsp)
    mov %rbx, GS_RBX(%rsp)
    mov %rbp, GS_RBP(%rsp)
    mov %rsi, GS_RSI(%rsp)
    mov %rdi, GS_RDI(%rsp)
    mov %r8, GS_R8(%rsp)
    mov %r9, GS_R9(%rsp)
    mov %r10, GS_R10(%rsp)
    mov %r11, GS_R11(%rsp)
    mov %r12, GS_R12(%rsp)
    mov %r13, GS_R13(%rsp)
    mov %r14, GS_R14(%rsp)
    mov %r15, GS_R15(%rsp)

    // Store the guest CR2.
    mov %cr2, %rax
    mov %rax, GS_CR2(%rsp)

    // Load vmx_state from RSP into RDI.
    mov %rsp, %rdi

    // Load the host callee save registers.
    mov HS_RBX(%rdi), %rbx
    mov HS_RSP(%rdi), %rsp
    mov HS_RBP(%rdi), %rbp
    mov HS_R12(%rdi), %r12
    mov HS_R13(%rdi), %r13
    mov HS_R14(%rdi), %r14
    mov HS_R15(%rdi), %r15

    // Load the host processor flags.
    pushq HS_RFLAGS(%rdi)
    popfq

    // Set up the return address.
    pushq HS_RIP(%rdi)

    // Call vmx_exit(vmx_state).
    sub $8, %rsp
    call vmx_exit
    add $8, %rsp

    // Return NO_ERROR, using the return address of vmx_enter pushed above.
    mov $NO_ERROR, %eax
    ret

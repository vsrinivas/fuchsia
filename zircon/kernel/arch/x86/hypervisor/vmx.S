// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>

#include <arch/x86/hypervisor/vmx_state.h>
#include <zircon/errors.h>

.text

/* zx_status_t vmx_enter_asm(VmxState* vmx_state) */
FUNCTION(vmx_enter_asm)
    // Save callee-saved registers and flags to stack.
    push %rbp
    push %r15
    push %r14
    push %r13
    push %r12
    push %rbx
    pushf

    // Store the host's stack pointer.
    mov %rsp, HS_RSP(%rdi)

    // We are going to trample RDI, so move it to RSP.
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
    jnz .Lresume

    // Launch the guest.
    //
    // Returns to `vmx_exit_asm` on success, or drops through on error.
    vmlaunch
    jmp .Lfailure

.Lresume:
    // Enter the guest.
    //
    // Returns to `vmx_exit_asm` on success, or drops through on error.
    vmresume

.Lfailure:
    // If we reach this state, the `vmlaunch` or `vmresume` has failed.
    //
    // Set the error code (RAX) to ZX_ERR_INTERNAL, and restore the host state.
    mov $ZX_ERR_INTERNAL, %rax
    jmp .Lrestore_host_regs

    // Return to the host after a `vmlaunch` or `vmresume`.
    //
    // When we return from a VM exit, vmx_state argument is stored in RSP. We
    // use this to restore the stack and registers to the state they were in
    // when vmx_enter was called.
.align 32
.global vmx_exit_asm
.hidden vmx_exit_asm
vmx_exit_asm:

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

    // Set function result to ZX_OK.
    xor %rax, %rax  // RAX = ZX_OK

    // Zero guest-controlled GPRs; this prevents speculative execution from
    // using guest-controlled values by limiting the lifetime of those values.
    xor %ebx, %ebx
    xor %ecx, %ecx
    xor %edx, %edx
    xor %esi, %esi
    xor %edi, %edi
    xor %r8, %r8
    xor %r9, %r9
    xor %r10, %r10
    xor %r11, %r11
    xor %r12, %r12
    xor %r13, %r13
    xor %r14, %r14
    xor %r15, %r15

.Lrestore_host_regs:
    // Restore the host registers.
    //
    // At this point, RSP contains vmx_state, and RAX contains the zx_status_t
    // error code.

    // Load vmx_state from RSP into RDI, and restore our stack.
    mov %rsp, %rdi
    mov HS_RSP(%rdi), %rsp

    // Restore callee-saved registers and flags from stack.
    popf
    pop %rbx
    pop %r12
    pop %r13
    pop %r14
    pop %r15
    pop %rbp

    ret

END_FUNCTION(vmx_enter_asm)

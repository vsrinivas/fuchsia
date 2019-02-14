// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/iframe.h>

// x86_uspace_entry(x86_iframe_t* iframe)
FUNCTION(x86_uspace_entry)
    /* push a fake 64bit interrupt stack frame and iret to it */
    push_value X86_IFRAME_OFFSET_USER_SS(%rdi) // ss
    push_value X86_IFRAME_OFFSET_USER_SP(%rdi) // sp
    push_value X86_IFRAME_OFFSET_FLAGS(%rdi)   // rflags
    push_value X86_IFRAME_OFFSET_CS(%rdi)      // cs
    push_value X86_IFRAME_OFFSET_IP(%rdi)      // pc

    ALL_CFI_UNDEFINED

    // Copy the remaining register state from |iframe|.
    movq X86_IFRAME_OFFSET_RAX(%rdi), %rax
    movq X86_IFRAME_OFFSET_RBX(%rdi), %rbx
    movq X86_IFRAME_OFFSET_RCX(%rdi), %rcx
    movq X86_IFRAME_OFFSET_RDX(%rdi), %rdx
    movq X86_IFRAME_OFFSET_RBP(%rdi), %rbp
    movq X86_IFRAME_OFFSET_RSI(%rdi), %rsi
    // Setting rdi deferred to last, we're still using it.
    movq X86_IFRAME_OFFSET_R8(%rdi), %r8
    movq X86_IFRAME_OFFSET_R9(%rdi), %r9
    movq X86_IFRAME_OFFSET_R10(%rdi), %r10
    movq X86_IFRAME_OFFSET_R11(%rdi), %r11
    movq X86_IFRAME_OFFSET_R12(%rdi), %r12
    movq X86_IFRAME_OFFSET_R13(%rdi), %r13
    movq X86_IFRAME_OFFSET_R14(%rdi), %r14
    movq X86_IFRAME_OFFSET_R15(%rdi), %r15
    movq X86_IFRAME_OFFSET_RDI(%rdi), %rdi

    // We need a register to zero out the segment registers.
    // Save %rax before the swapgs so that if this push faults then gs will
    // still be valid for the kernel. This isn't likely to happen, but we
    // play it safe.
    pushq %rax

    // We do not need to clear extended register state, since the kernel only
    // uses the general purpose registers, and the extended state is initialized
    // to a cleared state.

    swapgs

    xorl %eax, %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    popq %rax

    iretq
END_FUNCTION(x86_uspace_entry)

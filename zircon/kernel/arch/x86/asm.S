// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/defines.h>

/* void x86_64_context_switch(uint64_t *oldsp, uint64_t newsp) */
FUNCTION(x86_64_context_switch)
    /* save the old context and restore the new */
    /* This layout should match struct x86_64_context_switch_frame */
    push_reg %rbx
    push_reg %rbp
    push_reg %r12
    push_reg %r13
    push_reg %r14
    push_reg %r15

    movq %rsp,(%rdi)
    movq %rsi,%rsp

    pop_reg %r15
    pop_reg %r14
    pop_reg %r13
    pop_reg %r12
    pop_reg %rbp
    pop_reg %rbx

    retq
END_FUNCTION(x86_64_context_switch)

/* rep stos version of page zero */
FUNCTION(arch_zero_page)
    xorl    %eax, %eax /* set %rax = 0 */
    mov     $PAGE_SIZE >> 3, %rcx
    cld

    rep     stosq

    ret
END_FUNCTION(arch_zero_page)

// This clobbers %rax and memory below %rsp, but preserves all other registers.
FUNCTION(load_startup_idt)
    lea _idt_startup(%rip), %rax
    movw $(16 * 256) - 1, -16(%rsp)
    movq %rax, -16+2(%rsp)
    lidt -16(%rsp)
    ret
END_FUNCTION(load_startup_idt)

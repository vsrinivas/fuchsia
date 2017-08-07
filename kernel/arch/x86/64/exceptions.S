// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>

#define NUM_INT 256

/*
 * Please note that the macro for generating interrupt routine stubs relies
 * on the macro execution counter \@ which is shared by all invocations across
 * this compilation unit. Be careful when adding additional macros to this
 * file. In particular:
 * 1) No macros can be executed before def_isr (so \@ starts at zero).
 * 2) def_isr cannot have any macros (so \@ increments by one for each
 *    def_isr invocation).
 */

.text

/* interrupt service routine stubs */
_isr:
.macro def_isr
.pushsection .text
FUNCTION_LABEL(_isr_\@)
    .cfi_startproc simple
    .cfi_signal_frame
    /* Set CFA for an interrupt frame. */
.if \@ == 8 || (\@ >= 10 && \@ <= 14) || \@ == 17
    .cfi_def_cfa %rsp, (8 * 6)
.else
    .cfi_def_cfa %rsp, (8 * 5)
.endif
    .cfi_offset %rip, -(5 * 8)
    /* Mark each reg as having the same value as from the "calling" frame.
       This is the default state for callee-saved registers, but for completeness
       sake we do this for all of them. */
    ALL_CFI_SAME_VALUE
    /* Clear the AC flag to prevent ring 0 from performing data accesses to
     * ring 3 if SMAP is available.  If it was set, it will get restored by
     * iretd.  DO NOT REMOVE THIS CLAC, code in idt.c assumes it is here.
     * It MUST be the first instruction of this function. */
    clac
    /* We can't use push_value here: it is a macro invocation and using it
     * will screw up tracking of \@ == isr number. Instead we inline the .cfi
     * directives. */
.if \@ == 8 || (\@ >= 10 && \@ <= 14) || \@ == 17
    /* error code pushed by exception */
    pushq $\@              /* interrupt number */
    .cfi_adjust_cfa_offset 8
    jmp interrupt_common
.else
    pushq $0               /* fill in error code in iframe */
    .cfi_adjust_cfa_offset 8
    pushq $\@              /* interrupt number */
    .cfi_adjust_cfa_offset 8
    jmp interrupt_common
.endif
END_FUNCTION(_isr_\@)
.popsection
.pushsection .rodata
.quad _isr_\@
.popsection
.endm

.pushsection .rodata
/* build a table of isr entry points */
DATA(_isr_table)
.popsection
.rept NUM_INT
def_isr
.endr

FUNCTION_LABEL(interrupt_common)
    .cfi_startproc simple
    .cfi_signal_frame
    /* Set CFA for an interrupt frame. */
    .cfi_def_cfa %rsp, 7 * 8 /* hw + _isr_* push this many values */
    .cfi_offset %rip, -(5 * 8)
    /* Mark each reg as having the same value as from the "calling" frame.
       This is the default state for callee-saved registers, but for completeness
       sake we do this for all of them. */
    ALL_CFI_SAME_VALUE

    /* Clear the direction flag.  Without this, uses of string
       instructions, e.g. REP MOVS in memcpy() or inlined by the compiler,
       can go wrong and copy in the wrong direction, since this code may
       assume that the direction flag is unset. */
    cld

    /* Check to see if we came from user space by testing the PL of the
     * CS register that was saved on the stack automatically. Check for != 0.
     */
    testb $3, 0x18(%rsp)
    jz    1f

    /* swap gs to kernel space */
    swapgs

1:
    /* save general purpose registers */
    push_reg %r15
    push_reg %r14
    push_reg %r13
    push_reg %r12
    push_reg %r11
    push_reg %r10
    push_reg %r9
    push_reg %r8
    push_reg %rax
    push_reg %rcx
    push_reg %rdx
    push_reg %rbx
    push_reg %rbp
    push_reg %rsi
    push_reg %rdi

    movq %rsp, %rdi     /* pass the  iframe using rdi */

    call x86_exception_handler

/* A label to assist gdb's backtracing through kernel exceptions.
   When gdb sees this as the return address it knows it can fetch
   x86_iframe_t from $rsp. See scripts/magenta.elf-gdb.py. */
interrupt_common_iframe_set_up_for_debugger:

    /* restore general purpose registers */
    pop_reg %rdi
    pop_reg %rsi
    pop_reg %rbp
    pop_reg %rbx
    pop_reg %rdx
    pop_reg %rcx
    pop_reg %rax
    pop_reg %r8
    pop_reg %r9
    pop_reg %r10
    pop_reg %r11
    pop_reg %r12
    pop_reg %r13
    pop_reg %r14
    pop_reg %r15

    /* check if we're returning to user space as per before */
    testb $3, 0x18(%rsp)
    jz    1f

    /* swap gs back to user space */
    swapgs

1:
    /* drop vector number and error code*/
    add_to_sp 16

    iretq
END_FUNCTION(interrupt_common)

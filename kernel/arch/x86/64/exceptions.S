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
 * Please not that the macro for generating interrupt routine stubs relies
 * on macro execution counter which is shared by all invocation across this
 * compilation unit. Be careful when adding additional macros to this file.
 */

.text

/* interrupt service routine stubs */
_isr:
.macro def_isr
.pushsection .text
FUNCTION(_isr_\@)
    /* Clear the AC flag to prevent ring 0 from performing data accesses to
     * ring 3 if SMAP is available.  If it was set, it will get restored by
     * iretd.  DO NOT REMOVE THIS CLAC, code in idt.c assumes it is here.
     * It MUST be the first instruction of this function. */
    clac
.if \@ == 8 || (\@ >= 10 && \@ <= 14) || \@ == 17
    /* error code pushed by exception */
    pushq $\@                                /* interrupt number */
    jmp interrupt_common
.else
    pushq $0                                 /* fill in error code in iframe */
    pushq $\@                                /* interrupt number */
    jmp interrupt_common
.endif
.popsection
.pushsection .data
.quad _isr_\@
.popsection
.endm

.pushsection .data
/* build a table of isr entry points */
DATA(_isr_table)
.popsection
.rept NUM_INT
def_isr
.endr

FUNCTION(interrupt_common)
    /* Check to see if we came from user space by testing the PL of the
     * CS register that was saved on the stack automatically. Check for != 0.
     */
    testb $3, 0x18(%rsp)
    jz    1f

    /* swap gs to kernel space */
    swapgs

1:
    /* save general purpose registers */
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %r11
    pushq %r10
    pushq %r9
    pushq %r8
    pushq %rax
    pushq %rcx
    pushq %rdx
    pushq %rbx
    pushq %rbp
    pushq %rsi
    pushq %rdi

    movq %rsp, %rdi     /* pass the  iframe using rdi */

    call x86_exception_handler

/* A label to assist gdb's backtracing through kernel exceptions.
   When gdb sees this as the return address it knows it can fetch
   x86_iframe_t from $rsp. See scripts/lk.elf-gdb.py. */
interrupt_common_iframe_set_up_for_debugger:

    /* restore general purpose registers */
    popq %rdi
    popq %rsi
    popq %rbp
    popq %rbx
    popq %rdx
    popq %rcx
    popq %rax
    popq %r8
    popq %r9
    popq %r10
    popq %r11
    popq %r12
    popq %r13
    popq %r14
    popq %r15

    /* check if we're returning to user space as per before */
    testb $3, 0x18(%rsp)
    jz    1f

    /* swap gs back to user space */
    swapgs

1:
    /* drop vector number and error code*/
    addq $16, %rsp

    iretq

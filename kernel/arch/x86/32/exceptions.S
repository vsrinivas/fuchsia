// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2009 Corey Tabaka
// Copyright (c) 2015 Intel Corporation
// Copyright (c) 2016 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/x86/descriptor.h>

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
    pushl $\@                                /* interrupt number */
    jmp interrupt_common
.else
    pushl $0                                 /* fill in error code in iframe */
    pushl $\@                                /* interrupt number */
    jmp interrupt_common
.endif
.popsection
.pushsection .data
.long _isr_\@
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
    pushl %gs               /* save segment registers */
    pushl %fs
    pushl %es
    pushl %ds
    pusha                   /* save general purpose registers */
    movl $DATA_SELECTOR, %eax /* put known good value in segment registers */
    movl %eax, %gs
    movl %eax, %fs
    movl %eax, %es
    movl %eax, %ds

    movl %esp, %eax         /* store pointer to iframe */
    pushl %eax

    call x86_exception_handler

    popl %eax               /* drop pointer to iframe */

    popa                    /* restore general purpose registers */
    popl %ds                /* restore segment registers */
    popl %es
    popl %fs
    popl %gs
    addl $8, %esp           /* drop exception number and error code */
    iret

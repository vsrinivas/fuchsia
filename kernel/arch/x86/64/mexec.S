// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/defines.h>
#include <arch/x86/asm.h>
#include <arch/x86/descriptor.h>
#include <arch/x86/mmu.h>
#include <arch/x86/registers.h>
#include <asm.h>
#include <magenta/tls.h>
#include <mexec.h>

#define ENTRY64_ADDR 0x100020

.section .text
FUNCTION_LABEL(mexec_asm)
    /* Stash all the arguments passed in registers R8 - R13 */
    mov %r9,  %r13   /* Unused Arg */
    mov %r8,  %r12   /* Memmove Ops */
    mov %rcx, %r11   /* Unused Arg */
    mov %rdx, %r10   /* Unused Arg */
    mov %rsi, %r9    /* CR3 for Safe page tables */
    mov %rdi, %r8    /* Bootimage Address */

    /* Switch to the safe identity mapped page tables */
    mov  %r9, %cr3

    /* Load the kernel relocation op into ram */
    mov MEMMOV_OPS_DST_OFFSET (%r12), %rdi
    mov MEMMOV_OPS_SRC_OFFSET (%r12), %rsi
    mov MEMMOV_OPS_LEN_OFFSET (%r12), %rcx

    call .Lmexec_memmove

    /* Move the address of the bootdata into the appropriate register */
    mov %r8, %rsi

    /* Zero out some registers */ 
    xor %ebx, %ebx
    xor %edi, %edi
    xor %ebp, %ebp

    mov $ENTRY64_ADDR, %rbx
    mov (%rbx), %rax
    xor %rbx, %rbx

    /* Clear interrupts */
    cli

    /* See you on the other side! */
    jmp *%rax

    /* Crash, we should never reach here */
    ud2

.Lmexec_memmove:
    /* Move RCX bytes from RSI to RDI */
    cld               /* Clear the direction flag so that we're copying forward */
                      /* by default when we start */

    cmp %rsi, %rdi    /* Compare the src and dst registers to see if we need to */
                      /* copy forwards or backwards */

    jbe .Ldo_copy      /* if dst is greater than src, go ahead and do the copy */
                      /* forwards */

    mov %rcx, %rax    /* rcx and rax contain the number of bytes to be copied */
    sub $1,   %rax    /* Move rsi and rdi to the end of their respective buffers */
    add %rax, %rdi
    add %rax, %rsi

    std               /* Set the direction flag to 1. This will ensure that the */
                      /* copy happens from the back of the buffers to the front */

.Ldo_copy:

    rep movsb         /* copy RCX bytes from RSI to RDI */

    cld               /* Clear the direction flag since we may have polluted it */
                      /* if we did a copy backwards */
    ret

DATA(mexec_asm_end)

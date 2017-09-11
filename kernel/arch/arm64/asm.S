// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/asm_macros.h>

#define HCR_EL2_RW  (1 << 31)
#define SCR_EL3_HCE (1 << 1)
#define SCR_EL3_RW  (1 << 10)

/* void arm64_context_switch(vaddr_t *old_sp, vaddr_t new_sp); */
FUNCTION(arm64_context_switch)
    /* save old frame */
    /* This layout should match struct context_switch_frame */
    push_regs x29, x30
    push_regs x27, x28
    push_regs x25, x26
    push_regs x23, x24
    push_regs x21, x22
    push_regs x19, x20
    mrs  x19, tpidr_el0
    mrs  x20, tpidrro_el0
    push_regs x19, x20

    /* save old sp */
    mov  x15, sp
    str  x15, [x0]

    /* load new sp */
    mov  sp, x1

    /* restore new frame */
    pop_regs x19, x20
    msr  tpidr_el0, x19
    msr  tpidrro_el0, x20
    pop_regs x19, x20
    pop_regs x21, x22
    pop_regs x23, x24
    pop_regs x25, x26
    pop_regs x27, x28
    pop_regs x29, x30

    ret
END_FUNCTION(arm64_context_switch)

FUNCTION(arm64_elX_to_el1)
    mrs x9, CurrentEL

    cmp x9, #(0b01 << 2)
    bne .notEL1
    /* Already in EL1 */
    ret

.notEL1:
    cmp x9, #(0b10 << 2)
    beq .inEL2

    /* set EL2 to 64bit and enable HVC instruction */
    mrs x9, scr_el3
    orr x9, x9, #SCR_EL3_HCE
    orr x9, x9, #SCR_EL3_RW
    msr scr_el3, x9

    adr x9, .Ltarget
    msr elr_el3, x9

    mov x9, #((0b1111 << 6) | (0b0101)) /* EL1h runlevel */
    msr spsr_el3, x9
    b   .confEL1

.inEL2:
    /* Set the vector base for EL2 */
    adr_global x9, arm64_el2_exception_base
    msr vbar_el2, x9

    /* Ensure EL1 timers are properly configured, disable EL2 trapping of
        EL1 access to timer control registers.  Also clear virtual offset.
    */
    mrs x9, cnthctl_el2
    orr x9, x9, #3
    msr cnthctl_el2, x9
    msr cntvoff_el2, xzr

    /* clear out stage 2 translations */
    msr vttbr_el2, xzr

    adr x9, .Ltarget
    msr elr_el2, x9
    mov x9, #((0b1111 << 6) | (0b0101)) /* EL1h runlevel */
    msr spsr_el2, x9

.confEL1:
    /* disable EL2 coprocessor traps */
    mov x9, #0x33ff
    msr cptr_el2, x9

    /* set EL1 to 64bit */
    mov x9, #HCR_EL2_RW
    msr hcr_el2, x9

    /* disable EL1 FPU traps */
    mov x9, #(0b11<<20)
    msr cpacr_el1, x9

    /* set up the EL1 bounce interrupt */
    mov x9, sp
    msr sp_el1, x9

    isb
    eret

.Ltarget:
    ret
END_FUNCTION(arm64_elX_to_el1)

FUNCTION(arm64_get_secondary_sp)
    mrs     x9, mpidr_el1
    and     x9, x9, #0xffff             /* only use id/cluster */
    mov     x10, #SMP_MAX_CPUS

    adr_global x11, arm64_secondary_sp_list

.Lsp_loop:
    ldr     x12, [x11, #0]
    cmp     x12, x9
    beq     .Lsp_found
    add     x11, x11, #32
    subs    x10, x10, #1
    bne     .Lsp_loop
    mov     x0, xzr
    mov     x1, xzr
    ret

.Lsp_found:
    ldr     x0, [x11, #8]
    add     x1, x11, #32
    ret
END_FUNCTION(arm64_get_secondary_sp)

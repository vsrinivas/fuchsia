// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64/asm.h>
#include <arch/asm_macros.h>

#ifndef __has_feature
#define __has_feature(x) 0
#endif

#define CURRENTEL_EL1           (0b01 << 2)
#define CURRENTEL_EL2           (0b10 << 2)

#define CPACR_EL1_FPEN          (0b11 << 20)
#define ID_AA64PFR0_EL1_GIC     (0b1111 << 24)

#define CNTHCTL_EL2_EL1PCEN     (1 << 1)
#define CNTHCTL_EL2_EL1PCTEN    (1 << 0)
#define CPTR_EL2_RES1           0x33ff
#define HCR_EL2_RW              (1 << 31)
#define ICC_SRE_EL2_SRE         (1 << 0)
#define ICC_SRE_EL2_ENABLE      (1 << 3)

#define SCR_EL3_HCE             (1 << 8)
#define SCR_EL3_NS              (1 << 0)
#define SCR_EL3_RW              (1 << 10)

#define SPSR_ELX_DAIF           (0b1111 << 6)
#define SPSR_ELX_EL1H           (0b0101)

#define ICH_HCR_EL2             S3_4_C12_C11_0
#define ICC_SRE_EL2             S3_4_C12_C9_5

#if __has_feature(shadow_call_stack)
// void arm64_context_switch(addr_t* old_sp, addr_t new_sp, uintptr_t** old_scsp, uintptr_t* new_scsp);
#else
// void arm64_context_switch(addr_t* old_sp, addr_t new_sp);
#endif
FUNCTION(arm64_context_switch)
    /* The exclusive monitor is logically part of a thread's context.  Clear it to ensure the new
       thread does not complete the store half of a load-exclusive/store-exclusive started by the
       old thread.  If the context switch involves an exception return (eret) the monitor will be
       cleared automatically.  However, if the thread has yielded, an eret may not occur and so we
       must clear it here. */
    clrex

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
    mov  x9, sp
    str  x9, [x0]

    /* load new sp */
    mov  sp, x1

#if __has_feature(shadow_call_stack)
    str shadow_call_sp, [x2]
    mov shadow_call_sp, x3
#endif

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

    // Check the current exception level.
    cmp x9, CURRENTEL_EL1
    beq .Ltarget
    cmp x9, CURRENTEL_EL2
    beq .Lin_el2
    // Otherwise, we are in EL3.

    // Set EL2 to 64bit and enable the HVC instruction.
    mrs x9, scr_el3
    mov x10, SCR_EL3_NS | SCR_EL3_HCE | SCR_EL3_RW
    orr x9, x9, x10
    msr scr_el3, x9

    // Set the return address and exception level.
    adr x9, .Ltarget
    msr elr_el3, x9
    mov x9, SPSR_ELX_DAIF | SPSR_ELX_EL1H
    msr spsr_el3, x9

.Lin_el2:
    // Set the init vector table for EL2.
    adr_global x9, arm64_el2_init_table
    msr vbar_el2, x9

    // Disable EL1 timer traps and the timer offset.
    mrs x9, cnthctl_el2
    orr x9, x9, CNTHCTL_EL2_EL1PCEN | CNTHCTL_EL2_EL1PCTEN
    msr cnthctl_el2, x9
    msr cntvoff_el2, xzr

    // Disable stage 2 translations.
    msr vttbr_el2, xzr

    // Disable EL2 coprocessor traps.
    mov x9, CPTR_EL2_RES1
    msr cptr_el2, x9

    // Disable EL1 FPU traps.
    mov x9, CPACR_EL1_FPEN
    msr cpacr_el1, x9

    // Check whether the GIC system registers are supported.
    mrs x9, id_aa64pfr0_el1
    and x9, x9, ID_AA64PFR0_EL1_GIC
    cbz x9, .Lno_gic_sr

    // Enable the GIC system registers in EL2, and allow their use in EL1.
    mrs x9, ICC_SRE_EL2
    mov x10, ICC_SRE_EL2_ENABLE | ICC_SRE_EL2_SRE
    orr x9, x9, x10
    msr ICC_SRE_EL2, x9

    // Disable the GIC virtual CPU interface.
    msr ICH_HCR_EL2, xzr

.Lno_gic_sr:
    // Set EL1 to 64bit.
    mov x9, HCR_EL2_RW
    msr hcr_el2, x9

    // Set the return address and exception level.
    adr x9, .Ltarget
    msr elr_el2, x9
    mov x9, SPSR_ELX_DAIF | SPSR_ELX_EL1H
    msr spsr_el2, x9

    isb
    eret
    SPECULATION_POSTFENCE

.Ltarget:
    ret
END_FUNCTION(arm64_elX_to_el1)

FUNCTION(arm64_get_secondary_sp)
    mrs     x9, mpidr_el1
    ldr     x10, =0xff00ffffff  /* Mask for AFFx (cluster) ids */
    and     x9, x9, x10
    mov     x10, #SMP_MAX_CPUS

    adr_global x11, arm64_secondary_sp_list

.Lsp_loop:
    ldr     x12, [x11, #0]
    cmp     x12, x9
    beq     .Lsp_found
    add     x11, x11, #40
    subs    x10, x10, #1
    bne     .Lsp_loop
    mov     x0, xzr
    mov     x1, xzr
    mov     x2, xzr
    ret

.Lsp_found:
    ldr     x0, [x11, #8]
    ldr     x2, [x11, #16]
    add     x1, x11, #40
    ret
END_FUNCTION(arm64_get_secondary_sp)

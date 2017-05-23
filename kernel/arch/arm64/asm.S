// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <arch/asm_macros.h>

/* void arm64_context_switch(vaddr_t *old_sp, vaddr_t new_sp); */
FUNCTION(arm64_context_switch)
    /* save old frame */
    push x28, x29
    push x26, x27
    push x24, x25
    push x22, x23
    push x20, x21
    push x18, x19
    mrs  x18, tpidr_el0
    mrs  x19, tpidrro_el0
    push x18, x19
    push x30, xzr

    /* save old sp */
    mov  x15, sp
    str  x15, [x0]

    /* load new sp */
    mov  sp, x1

    /* restore new frame */
    pop  x30, xzr
    pop  x18, x19
    msr  tpidr_el0, x18
    msr  tpidrro_el0, x19
    pop  x18, x19
    pop  x20, x21
    pop  x22, x23
    pop  x24, x25
    pop  x26, x27
    pop  x28, x29

    ret
END(arm64_context_switch)

FUNCTION(arm64_el3_to_el1)
    /* set EL2 to 64bit */
    mrs x0, scr_el3
    orr x0, x0, #(1<<10)
    msr scr_el3, x0

    /* set EL1 to 64bit */
    mov x0, #(1<<31)
    msr hcr_el2, x0

    /* disable EL2 coprocessor traps */
    mov x0, #0x33ff
    msr cptr_el2, x0

    /* disable EL1 FPU traps */
    mov x0, #(0b11<<20)
    msr cpacr_el1, x0

    /* set up the EL1 bounce interrupt */
    mov x0, sp
    msr sp_el1, x0

    adr x0, .Ltarget
    msr elr_el3, x0

    mov x0, #((0b1111 << 6) | (0b0101)) /* EL1h runlevel */
    msr spsr_el3, x0
    isb

    eret
END(arm64_el3_to_el1)

FUNCTION(arm64_elX_to_el1)
    mrs x9, CurrentEL

    cmp x9, #(0b01 << 2)
    bne .notEL1
    /* Already in EL1 */
    ret

.notEL1:
    cmp x9, #(0b10 << 2)
    beq .inEL2


    /* set EL2 to 64bit */
    mrs x9, scr_el3
    orr x9, x9, #(1<<10)
    msr scr_el3, x9


    adr x9, .Ltarget
    msr elr_el3, x9

    mov x9, #((0b1111 << 6) | (0b0101)) /* EL1h runlevel */
    msr spsr_el3, x9
    b   .confEL1

.inEL2:
    adr x9, .Ltarget
    msr elr_el2, x9
    mov x9, #((0b1111 << 6) | (0b0101)) /* EL1h runlevel */
    msr spsr_el2, x9



.confEL1:
    /* disable EL2 coprocessor traps */
    mov x9, #0x33ff
    msr cptr_el2, x9

    /* set EL1 to 64bit */
    mov x9, #(1<<31)
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
END(arm64_elX_to_el1)

#if WITH_SMP
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

END(arm64_get_secondary_sp)
#endif

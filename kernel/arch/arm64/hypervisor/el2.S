// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64/el2_state.h>
#include <arch/arm64/mmu.h>
#include <arch/asm_macros.h>
#include <asm.h>
#include <zircon/errors.h>

#define CNTHCTL_EL2_EL1PCTEN            BIT_32(0)
#define CNTHCTL_EL2_EL1PCEN             BIT_32(1)

#define CPTR_EL2_TFP_SHIFT              10
#define CPTR_EL2_TFP                    BIT_32(CPTR_EL2_TFP_SHIFT)
#define CPTR_EL2_RES1                   0x33ff

#define ESR_EL2_EC_FP                   0x07
#define ESR_EL2_EC_HVC                  0x16
#define ESR_EL2_EC_SHIFT                26
#define ESR_EL2_ISS_MASK                0x01ffffff

#define XTCR_EL2_PS_SHIFT               16

// NOTE(abdulla): This excludes the top bit, as it is too large for VTCR_EL2.PS.
#define ID_AA64MMFR0_EL1_PARANGE_MASK   0x07

.section .text.el2,"ax",@progbits
.align 12

.macro fp_state inst, off
    add x9, x9, \off
    \inst q0, q1, [x9, FS_Q(0)]
    \inst q2, q3, [x9, FS_Q(2)]
    \inst q4, q5, [x9, FS_Q(4)]
    \inst q6, q7, [x9, FS_Q(6)]
    \inst q8, q9, [x9, FS_Q(8)]
    \inst q10, q11, [x9, FS_Q(10)]
    \inst q12, q13, [x9, FS_Q(12)]
    \inst q14, q15, [x9, FS_Q(14)]
    \inst q16, q17, [x9, FS_Q(16)]
    \inst q18, q19, [x9, FS_Q(18)]
    \inst q20, q21, [x9, FS_Q(20)]
    \inst q22, q23, [x9, FS_Q(22)]
    \inst q24, q25, [x9, FS_Q(24)]
    \inst q26, q27, [x9, FS_Q(26)]
    \inst q28, q29, [x9, FS_Q(28)]
    \inst q30, q31, [x9, FS_Q(30)]
.ifc "\inst", "ldp"
    ldr x10, [x9, FS_FPSR]
    msr fpsr, x10
    ldr x10, [x9, FS_FPCR]
    msr fpcr, x10
.else
    mrs x10, fpsr
    str x10, [x9, FS_FPSR]
    mrs x10, fpcr
    str x10, [x9, FS_FPCR]
.endif
    sub x9, x9, \off
.endm

.macro system_register inst, off, sysreg
.ifc "\inst", "ldr"
    ldr x11, [x10, \off]
    msr \sysreg, x11
.else
    mrs x11, \sysreg
    str x11, [x10, \off]
.endif
.endm

.macro system_state inst, off
    add x10, x9, \off
    system_register \inst, SS_SP_EL0, sp_el0
    system_register \inst, SS_TPIDR_EL0, tpidr_el0
    system_register \inst, SS_TPIDRRO_EL0, tpidrro_el0

    system_register \inst, SS_CNTKCTL_EL1, cntkctl_el1
    system_register \inst, SS_CONTEXTIDR_EL1, contextidr_el1
    system_register \inst, SS_CPACR_EL1, cpacr_el1
    system_register \inst, SS_CSSELR_EL1, csselr_el1
    system_register \inst, SS_ELR_EL1, elr_el1
    system_register \inst, SS_ESR_EL1, esr_el1
    system_register \inst, SS_FAR_EL1, far_el1
    system_register \inst, SS_MAIR_EL1, mair_el1
    system_register \inst, SS_MDSCR_EL1, mdscr_el1
    system_register \inst, SS_PAR_EL1, par_el1
    system_register \inst, SS_SCTLR_EL1, sctlr_el1
    system_register \inst, SS_SP_EL1, sp_el1
    system_register \inst, SS_SPSR_EL1, spsr_el1
    system_register \inst, SS_TCR_EL1, tcr_el1
    system_register \inst, SS_TPIDR_EL1, tpidr_el1
    system_register \inst, SS_TTBR0_EL1, ttbr0_el1
    system_register \inst, SS_TTBR1_EL1, ttbr1_el1
    system_register \inst, SS_VBAR_EL1, vbar_el1

    system_register \inst, SS_ELR_EL2, elr_el2
    system_register \inst, SS_SPSR_EL2, spsr_el2
    system_register \inst, SS_VMPIDR_EL2, vmpidr_el2
.endm

.macro host_state inst, off
    add x10, x9, \off
.ifc "\inst", "ldp"
    ldr x18, [x10, HS_X(0)]
.else
    str x18, [x10, HS_X(0)]
.endif
    \inst x19, x20, [x10, HS_X(1)]
    \inst x21, x22, [x10, HS_X(3)]
    \inst x23, x24, [x10, HS_X(5)]
    \inst x25, x26, [x10, HS_X(7)]
    \inst x27, x28, [x10, HS_X(9)]
    \inst x29, x30, [x10, HS_X(11)]
.endm

.macro guest_state inst
    \inst x0, x1, [x9, GS_X(0)]
    \inst x2, x3, [x9, GS_X(2)]
    \inst x4, x5, [x9, GS_X(4)]
    \inst x6, x7, [x9, GS_X(6)]
    \inst x10, x11, [x9, GS_X(10)]
    \inst x12, x13, [x9, GS_X(12)]
    \inst x14, x15, [x9, GS_X(14)]
    \inst x16, x17, [x9, GS_X(16)]
    \inst x18, x19, [x9, GS_X(18)]
    \inst x20, x21, [x9, GS_X(20)]
    \inst x22, x23, [x9, GS_X(22)]
    \inst x24, x25, [x9, GS_X(24)]
    \inst x26, x27, [x9, GS_X(26)]
    \inst x28, x29, [x9, GS_X(28)]
.ifc "\inst", "ldp"
    ldr x30, [x9, GS_X(30)]
.else
    str x30, [x9, GS_X(30)]
.endif
.endm

.macro guest_x9_state inst, reg
    \inst x8, \reg, [x9, GS_X(8)]
.endm

.macro guest_enter_state
    ldr x10, [x9, GS_CNTV_CVAL_EL0]
    msr cntv_cval_el0, x10
    ldr x10, [x9, GS_CNTV_CTL_EL0]
    msr cntv_ctl_el0, x10
.endm

.macro guest_exit_state
    mrs x10, cntv_ctl_el0
    str x10, [x9, GS_CNTV_CTL_EL0]
    mrs x10, cntv_cval_el0
    str x10, [x9, GS_CNTV_CVAL_EL0]
    mrs x10, esr_el2
    str x10, [x9, GS_ESR_EL2]
    mrs x10, far_el2
    str x10, [x9, GS_FAR_EL2]
    mrs x10, hpfar_el2
    // This is not described well in the manual, but HPFAR_EL2 does not contain
    // the lower 8 bits of the IPA, so it must be shifted.
    lsl x10, x10, 8
    str x10, [x9, GS_HPFAR_EL2]
.endm

.macro switch_to_guest
    msr vttbr_el2, x0
    isb
.endm

.macro switch_to_host
    msr vttbr_el2, xzr
    isb
.endm

.macro exception_return literal
    mov x0, \literal
    eret
.endm

.macro pop_stack
    add sp, sp, 16
.endm

.macro hvc_jump table size
    mrs x9, esr_el2

    // Check ESR_EL2.EC to determine what caused the exception.
    lsr x10, x9, ESR_EL2_EC_SHIFT
    cmp x10, ESR_EL2_EC_HVC
    b.ne .Linvalid_args_for_\table

    // Check ESR_EL2.ICC to determine whether the HVC index is in range.
    and x10, x9, ESR_EL2_ISS_MASK
    cmp x10, \size
    b.ge .Linvalid_args_for_\table

    // Branch to the jump table.
    adr x9, \table
    add x9, x9, x10, lsl 2
    br x9

.Linvalid_args_for_\table:
    exception_return ZX_ERR_INVALID_ARGS
.endm

.macro guest_exit return_code
    // We push X9 onto the stack so we have one scratch register. We only use
    // X9 here, so that we don't accidentally trample the guest state.
    str x9, [sp, -16]!
    mov x9, \return_code
    str x9, [sp, 8]
.endm

.macro entry_init
.align 7
    hvc_jump .Linit_table 4
.Linit_table:
    b el2_hvc_psci
    b el2_hvc_mexec
    b el2_hvc_on
    b el2_hvc_tlbi
.endm

.macro entry_sync return_code
.align 7
    guest_exit \return_code

    // Check VTTBR_EL2 to determine whether the exception came from the guest or
    // from the host.
    mrs x9, vttbr_el2
    cbnz x9, el2_guest_exit_or_fp_resume

    // The exception came from the host, so there is no guest state to preserve.
    pop_stack

    // If we got to here, the exception came from the host or EL2.
    // Continue execution through a jump table based on the HVC index.
    hvc_jump .Lsync_table 6
.Lsync_table:
    b el2_hvc_psci
    b el2_hvc_mexec
    b el2_hvc_off
    b el2_hvc_tlbi
    b el2_hvc_resume
    b el2_hvc_sysreg
.endm

.macro entry_irq return_code
.align 7
    guest_exit \return_code
    b el2_guest_exit
.endm

.macro entry_invalid_exception
.align 7
    // If we got to here, the exception came from the host or EL2.
    // We reset, as something unexpected happened.
    mov x0, xzr
    b psci_system_reset
.endm

// We have two vector tables that we switch between, init and exec. The reason
// is that we need to use the stack to temporarily save registers when we exit
// from a guest. However, that stack may have not been set up, and therefore we
// can not unconditionally use it. We use the init vector table to set up the
// stack and hypervisor state, and we use the exec vector table to maintain
// execution of the hypervisor.

.align 10
FUNCTION_LABEL(arm64_el2_init_table)
    /* exceptions from current EL, using SP0 */
    entry_invalid_exception
    entry_invalid_exception
    entry_invalid_exception
    entry_invalid_exception

    /* exceptions from current EL, using SPx */
    entry_invalid_exception
    entry_invalid_exception
    entry_invalid_exception
    entry_invalid_exception

    /* exceptions from lower EL, running arm64 */
    entry_init
    entry_invalid_exception
    entry_invalid_exception
    entry_invalid_exception

    /* exceptions from lower EL, running arm32 */
    entry_invalid_exception
    entry_invalid_exception
    entry_invalid_exception
    entry_invalid_exception

.align 10
FUNCTION_LABEL(arm64_el2_exec_table)
    /* exceptions from current EL, using SP0 */
    entry_invalid_exception
    entry_invalid_exception
    entry_invalid_exception
    entry_invalid_exception

    /* exceptions from current EL, using SPx */
    entry_invalid_exception
    entry_invalid_exception
    entry_invalid_exception
    entry_invalid_exception

    /* exceptions from lower EL, running arm64 */
    entry_sync ZX_OK
    entry_irq ZX_ERR_NEXT
    entry_invalid_exception
    entry_invalid_exception

    /* exceptions from lower EL, running arm32 */
    entry_invalid_exception
    entry_invalid_exception
    entry_invalid_exception
    entry_invalid_exception

// zx_status_t arm64_el2_on(zx_paddr_t ttbr0, zx_paddr_t stack_top);
//
// |stack_top| must point to the physical address of a contiguous stack.
FUNCTION(arm64_el2_on)
    hvc 2
    ret
END_FUNCTION(arm64_el2_on)
FUNCTION_LABEL(el2_hvc_on)
    // Setup the EL2 translation table.
    msr ttbr0_el2, x0

    // Setup the EL2 stack pointer.
    mov sp, x1

    // Load PARange from ID_AA64MMFR0_EL1.
    mrs x10, id_aa64mmfr0_el1
    and x10, x10, ID_AA64MMFR0_EL1_PARANGE_MASK
    lsl x10, x10, XTCR_EL2_PS_SHIFT

    // Setup the virtualisation translation control.
    movlit x9, MMU_VTCR_EL2_FLAGS
    // Combine MMU_VTCR_EL2_FLAGS with xTCR_EL2.PS.
    orr x9, x9, x10
    msr vtcr_el2, x9

    // Setup the EL2 translation control.
    movlit x9, MMU_TCR_EL2_FLAGS
    // Combine MMU_TCR_EL2_FLAGS with xTCR_EL2.PS.
    orr x9, x9, x10
    msr tcr_el2, x9

    // Setup the EL2 memory attributes.
    movlit x9, MMU_MAIR_VAL
    msr mair_el2, x9
    isb

    // Invalidate all EL2 TLB entries.
    tlbi alle2
    dsb sy

    // Enable the MMU, I-cache, D-cache, and stack alignment checking.
    movlit x9, SCTLR_ELX_M | SCTLR_ELX_C | SCTLR_ELX_SA | SCTLR_ELX_I | SCTLR_EL2_RES1
    msr sctlr_el2, x9
    isb

    // Setup the exec vector table for EL2.
    adr_global x9, arm64_el2_exec_table
    msr vbar_el2, x9

    exception_return ZX_OK

FUNCTION_LABEL(el2_hvc_psci)
    smc 0
    eret

FUNCTION_LABEL(el2_hvc_mexec)
    br x0

// zx_status_t arm64_el2_off();
FUNCTION(arm64_el2_off)
    hvc 2
    ret
END_FUNCTION(arm64_el2_off)
FUNCTION_LABEL(el2_hvc_off)
    // Disable the MMU, but enable I-cache, D-cache, and stack alignment checking.
    movlit x9, SCTLR_ELX_C | SCTLR_ELX_SA | SCTLR_ELX_I | SCTLR_EL2_RES1
    msr sctlr_el2, x9
    isb

    // Invalidate all EL2 TLB entries.
    tlbi alle2
    isb

    // Setup the init vector table for EL2.
    adr_global x9, arm64_el2_init_table
    msr vbar_el2, x9
    isb

    exception_return ZX_OK

// zx_status_t arm64_el2_tlbi_ipa(zx_paddr_t vttbr, zx_vaddr_t addr, bool terminal);
FUNCTION(arm64_el2_tlbi_ipa)
    mov x7, 0
    hvc 3
    ret
END_FUNCTION(arm64_el2_tlbi_ipa)

// zx_status_t arm64_el2_tlbi_vmid(zx_paddr_t vttbr);
FUNCTION(arm64_el2_tlbi_vmid)
    mov x7, 1
    hvc 3
    ret
END_FUNCTION(arm64_el2_tlbi_vmid)

FUNCTION_LABEL(el2_hvc_tlbi)
    switch_to_guest

    cbz x7, el2_hvc_tlbi_ipa
    b el2_hvc_tlbi_vmid

.Ltlbi_exit:
    switch_to_host
    exception_return ZX_OK

FUNCTION_LABEL(el2_hvc_tlbi_ipa)
    // TLBI IPAS2* instructions take bits [51:12] of the IPA.
    lsr x1, x1, 12

    // Invalidate IPA. Based on ARM DEN 0024A, page 12-5.
    dsb ishst
    cbnz x2, .Lterminal
    tlbi ipas2e1is, x1
    b .Lsync
.Lterminal:
    tlbi ipas2le1is, x1
.Lsync:
    dsb ish
    tlbi vmalle1is
    dsb ish
    isb
    b .Ltlbi_exit

FUNCTION_LABEL(el2_hvc_tlbi_vmid)
    // Invalidate VMID. Based on ARM DEN 0024A, page 12-5.
    dsb ishst
    tlbi vmalls12e1is
    dsb ish
    isb
    b .Ltlbi_exit

// zx_status_t arm64_el2_resume(zx_paddr_t vttbr, zx_paddr_t state, uint64_t hcr);
FUNCTION(arm64_el2_resume)
    hvc 4
    ret
END_FUNCTION(arm64_el2_resume)
FUNCTION_LABEL(el2_hvc_resume)
    switch_to_guest

    // Save El2State into tpidr_el2.
    msr tpidr_el2, x1
    mov x9, x1

    // If the guest is being run for the first time, invalidate all VMID TLB
    // entries in case the VMID has been used previously.
    ldr x10, [x9, ES_RESUME]
    cbnz x10, .Lresume
    tlbi vmalle1
    mov x10, 1
    str x10, [x9, ES_RESUME]
    dsb nshst

.Lresume:
    // Set the hypervisor control register.
    msr hcr_el2, x2

    // Disable access to physical timer.
    mov x10, CNTHCTL_EL2_EL1PCTEN
    msr cnthctl_el2, x10

    // Enable floating-point traps.
    movlit x10, CPTR_EL2_RES1 | CPTR_EL2_TFP
    msr cptr_el2, x10
    isb

    host_state stp, HS_X18
    system_state str, HS_SYSTEM_STATE
    guest_enter_state
    system_state ldr, GS_SYSTEM_STATE
    guest_state ldp
    guest_x9_state ldp, x9

    // Return to guest.
    eret

FUNCTION_LABEL(el2_guest_exit_or_fp_resume)
    // Check ESR_EL2.EC to determine whether the exception was due to a
    // floating-point trap.
    mrs x9, esr_el2
    lsr x9, x9, ESR_EL2_EC_SHIFT
    cmp x9, ESR_EL2_EC_FP
    b.eq el2_fp_resume

FUNCTION_LABEL(el2_guest_exit)
    // Load El2State from tpidr_el2.
    mrs x9, tpidr_el2

    guest_state stp
    // Load X9 from the stack, and save it in GuestState.
    ldr x10, [sp]
    guest_x9_state stp, x10
    system_state str, GS_SYSTEM_STATE
    guest_exit_state
    system_state ldr, HS_SYSTEM_STATE
    host_state ldp, HS_X18

    mrs x10, cptr_el2
    tbnz x10, CPTR_EL2_TFP_SHIFT, .Lfp_untrap

    // Restore floating-point state if it was modified.
    fp_state stp, GS_FP_STATE
    fp_state ldp, HS_FP_STATE
    b .Lfp_done

.Lfp_untrap:
    // Disable floating-point traps.
    mov x10, CPTR_EL2_RES1
    msr cptr_el2, x10

.Lfp_done:
    // Disable virtual timer set by guest, and enable access to physical timer.
    msr cntv_ctl_el0, xzr
    mov x10, CNTHCTL_EL2_EL1PCTEN | CNTHCTL_EL2_EL1PCEN
    msr cnthctl_el2, x10

    // Disable guest traps, and ensure EL1 is arm64.
    mov x10, HCR_EL2_RW
    msr hcr_el2, x10
    isb

    switch_to_host

    // Return to host.
    ldr x0, [sp, 8]
    pop_stack
    eret

FUNCTION_LABEL(el2_fp_resume)
    // Save x10 so we have an extra register for floating-point state swapping.
    // We're returning to the guest so we don't need the return code in [sp, 8].
    str x10, [sp, 8]

    // Disable floating-point traps and reset exception syndrome.
    mov x9, CPTR_EL2_RES1
    msr cptr_el2, x9
    msr esr_el2, xzr
    isb

    // Load El2State from tpidr_el2.
    mrs x9, tpidr_el2

    fp_state stp, HS_FP_STATE
    fp_state ldp, GS_FP_STATE

    // Return to guest.
    ldp x9, x10, [sp], 16
    eret
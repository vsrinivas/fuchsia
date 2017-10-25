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

#define ESR_EL2_EC_HVC                  0x16
#define ESR_EL2_EC_SHIFT                26
#define ESR_EL2_ISS_MASK                0x01ffffff

#define HCR_EL2_VM                      (1 << 0)
#define HCR_EL2_PTW                     (1 << 2)
#define HCR_EL2_FMO                     (1 << 3)
#define HCR_EL2_IMO                     (1 << 4)
#define HCR_EL2_AMO                     (1 << 5)
#define HCR_EL2_VF                      (1 << 6)
#define HCR_EL2_VI                      (1 << 7)
#define HCR_EL2_VSE                     (1 << 8)
#define HCR_EL2_DC                      (1 << 12)
#define HCR_EL2_TWI                     (1 << 13)
#define HCR_EL2_TWE                     (1 << 14)
#define HCR_EL2_TSC                     (1 << 19)
#define HCR_EL2_TVM                     (1 << 26)
#define HCR_EL2_RW                      (1 << 31)

#define SCTLR_EL2_M                     (1 << 0)
#define SCTLR_EL2_A                     (1 << 1)
#define SCTLR_EL2_C                     (1 << 2)
#define SCTLR_EL2_SA                    (1 << 3)
#define SCTLR_EL2_I                     (1 << 12)
#define SCTLR_EL2_RES1                  0x30c50830

#define XTCR_EL2_PS_SHIFT               16

// NOTE(abdulla): This excludes the top bit, as it is too large for VTCR_EL2.PS.
#define ID_AA64MMFR0_EL1_PARANGE_MASK   0x07

#define VECTOR_TABLE_SIZE               0x0800

.section .text.el2,"ax",@progbits
.align 12

.macro system_register inst, off, sysreg
.ifc "\inst", "ldr"
    mov x11, \off
    ldr x10, [x9, x11]
    msr \sysreg, x10
.else
    mrs x10, \sysreg
    mov x11, \off
    str x10, [x9, x11]
.endif
.endm

.macro system_state inst, off
    system_register \inst, \off + SS_CNTKCTL_EL1, cntkctl_el1
    system_register \inst, \off + SS_CONTEXTIDR_EL1, contextidr_el1
    system_register \inst, \off + SS_CPACR_EL1, cpacr_el1
    system_register \inst, \off + SS_CSSELR_EL1, csselr_el1
    system_register \inst, \off + SS_ESR_EL1, esr_el1
    system_register \inst, \off + SS_FAR_EL1, far_el1
    system_register \inst, \off + SS_MAIR_EL1, mair_el1
    system_register \inst, \off + SS_MDSCR_EL1, mdscr_el1
    system_register \inst, \off + SS_SCTLR_EL1, sctlr_el1
    system_register \inst, \off + SS_SP_EL1, sp_el1
    system_register \inst, \off + SS_TCR_EL1, tcr_el1
    system_register \inst, \off + SS_TPIDR_EL1, tpidr_el1
    system_register \inst, \off + SS_TTBR0_EL1, ttbr0_el1
    system_register \inst, \off + SS_TTBR1_EL1, ttbr1_el1
    system_register \inst, \off + SS_VBAR_EL1, vbar_el1

    system_register \inst, \off + SS_ELR_EL2, elr_el2
    system_register \inst, \off + SS_SPSR_EL2, spsr_el2
.endm

.macro host_state inst
.ifc "\inst", "ldp"
    ldr x18, [x9, #HS_X(0)]
.else
    str x18, [x9, #HS_X(0)]
.endif
    \inst x19, x20, [x9, #HS_X(1)]
    \inst x21, x22, [x9, #HS_X(3)]
    \inst x23, x24, [x9, #HS_X(5)]
    \inst x25, x26, [x9, #HS_X(7)]
    \inst x27, x28, [x9, #HS_X(9)]
    \inst x29, x30, [x9, #HS_X(11)]
.endm

.macro guest_state inst
    \inst x0, x1, [x9, #GS_X(0)]
    \inst x2, x3, [x9, #GS_X(2)]
    \inst x4, x5, [x9, #GS_X(4)]
    \inst x6, x7, [x9, #GS_X(6)]
    \inst x10, x11, [x9, #GS_X(10)]
    \inst x12, x13, [x9, #GS_X(12)]
    \inst x14, x15, [x9, #GS_X(14)]
    \inst x16, x17, [x9, #GS_X(16)]
    \inst x18, x19, [x9, #GS_X(18)]
    \inst x20, x21, [x9, #GS_X(20)]
    \inst x22, x23, [x9, #GS_X(22)]
    \inst x24, x25, [x9, #GS_X(24)]
    \inst x26, x27, [x9, #GS_X(26)]
    \inst x28, x29, [x9, #GS_X(28)]
.ifc "\inst", "ldp"
    ldr x30, [x9, #GS_X(30)]
.else
    str x30, [x9, #GS_X(30)]
.endif
.endm

.macro guest_x9_state inst, reg
    \inst x8, \reg, [x9, #GS_X(8)]
.endm

.macro guest_exit_state
    mrs x10, esr_el2
    str x10, [x9, #GS_ESR_EL2]
    mrs x10, hpfar_el2
    // This is not described well in the manual, but HPFAR_EL2 does not contain
    // the lower 8 bits of the IPA, so it must be shifted.
    lsl x10, x10, #8
    str x10, [x9, #GS_HPFAR_EL2]
.endm

.macro exception_return literal
    movlit x0, \literal
    eret
.endm

.macro pop_stack
    add sp, sp, #16
.endm

.macro hvc_jump table size
    mrs x9, esr_el2

    // Check ESR_EL2.EC to determine what caused the exception.
    lsr x10, x9, #ESR_EL2_EC_SHIFT
    cmp x10, #ESR_EL2_EC_HVC
    b.ne .Linvalid_args_for_\table

    // Check ESR_EL2.ICC to determine whether the HVC index is in range.
    and x10, x9, #ESR_EL2_ISS_MASK
    cmp x10, \size
    b.ge .Linvalid_args_for_\table

    // Branch to the jump table.
    adr x9, \table
    add x9, x9, x10, lsl #2
    br x9

.Linvalid_args_for_\table:
    exception_return ZX_ERR_INVALID_ARGS
.endm

.macro entry_init
.align 7
    hvc_jump .Linit_table 4
.Linit_table:
    b el2_hvc_psci
    b el2_hvc_on
    b el2_hvc_tlbi_ipa
    b el2_hvc_tlbi_vmid
.endm

.macro entry_exec name return_code
.align 7
    // We push X9 onto the stack so we have one scratch register. We only use
    // X9 here, so that we don't accidentally trample the guest state.
    str x9, [sp, #-16]!
    movlit x9, \return_code
    str x9, [sp, #8]

    // Check VTTBR_EL2 to determine whether the exception came from the guest or
    // from the host.
    mrs x9, vttbr_el2
    cbnz x9, el2_guest_exit

    // The exception came from the host, so there is no guest state to preserve.
    pop_stack

    // Continue execution through a jump table based on the HVC index.
    hvc_jump .L\name\()_exec_table 5
.L\name\()_exec_table:
    b el2_hvc_psci
    b el2_hvc_off
    b el2_hvc_tlbi_ipa
    b el2_hvc_tlbi_vmid
    b el2_hvc_resume
.endm

.macro entry_invalid_exception
.align 7
    // TODO(abdulla): Check VMID from VTTBR_EL2. ERET to host with error. If
    // VMID was not 0, terminate guest.
    hlt #0
.endm

// We have two vector tables that we switch between, init and exec. The reason
// is that we need to use the stack to temporarily save registers when we exit
// from a guest. However, that stack may have not been set up, and therefore we
// can not unconditionally use it. We use the init vector table to set up the
// stack and hypervisor state, and we use the exec vector table to maintain
// execution of the hypervisor.

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
    entry_exec sync, ZX_OK
    entry_exec irq, ZX_ERR_NEXT
    entry_invalid_exception
    entry_invalid_exception

    /* exceptions from lower EL, running arm32 */
    entry_invalid_exception
    entry_invalid_exception
    entry_invalid_exception
    entry_invalid_exception

// zx_status_t arm64_el2_on(zx_paddr_t stack_top, zx_paddr_t ttbr0);
//
// |stack_top| must point to the physical address of a contiguous stack.
FUNCTION(arm64_el2_on)
    hvc #1
    ret
END_FUNCTION(arm64_el2_on)
FUNCTION_LABEL(el2_hvc_on)
    // Load PARange from ID_AA64MMFR0_EL1.
    mrs x10, id_aa64mmfr0_el1
    and x10, x10, #ID_AA64MMFR0_EL1_PARANGE_MASK
    lsl x10, x10, #XTCR_EL2_PS_SHIFT

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

    // Setup the EL2 translation table.
    msr ttbr0_el2, x1
    isb

    // Enable the MMU, I-cache, D-cache, and all alignment checking.
    movlit x9, SCTLR_EL2_M | SCTLR_EL2_A | SCTLR_EL2_C | SCTLR_EL2_SA | SCTLR_EL2_I | \
               SCTLR_EL2_RES1
    msr sctlr_el2, x9
    isb

    // Invalidate all EL2 TLB entries.
    tlbi alle2
    isb

    // Setup the EL2 stack pointer.
    mov sp, x0

    // Setup the exec vector table for EL2.
    mrs x9, vbar_el2
    add x9, x9, #VECTOR_TABLE_SIZE
    msr vbar_el2, x9
    isb

    exception_return ZX_OK

FUNCTION_LABEL(el2_hvc_psci)
    smc #0
    eret

// zx_status_t arm64_el2_off();
FUNCTION(arm64_el2_off)
    hvc #1
    ret
END_FUNCTION(arm64_el2_off)
FUNCTION_LABEL(el2_hvc_off)
    // Disable the MMU, but enable I-cache, D-cache, and all alignment checking.
    movlit x9, SCTLR_EL2_A | SCTLR_EL2_C | SCTLR_EL2_SA | SCTLR_EL2_I | SCTLR_EL2_RES1
    msr sctlr_el2, x9
    isb

    // Invalidate all EL2 TLB entries.
    tlbi alle2
    isb

    // Setup the init vector table for EL2.
    mrs x9, vbar_el2
    sub x9, x9, #VECTOR_TABLE_SIZE
    msr vbar_el2, x9
    isb

    exception_return ZX_OK

// void arm64_el2_tlbi_ipa(uint8_t vmid, zx_vaddr_t addr);
FUNCTION(arm64_el2_tlbi_ipa)
    hvc #2
    ret
END_FUNCTION(arm64_el2_tlbi_ipa)
FUNCTION_LABEL(el2_hvc_tlbi_ipa)
    // Switch to the guest.
    msr vttbr_el2, x0
    isb

    // Invalidate IPA. Based on ARM DEN 0024A, page 12-5.
    dsb ishst
    tlbi ipas2e1is, x1
    dsb ish
    isb

    // Switch to the host.
    msr vttbr_el2, xzr
    isb

    exception_return ZX_OK

// void arm64_el2_tlbi_vmid(uint8_t vmid);
FUNCTION(arm64_el2_tlbi_vmid)
    hvc #3
    ret
END_FUNCTION(arm64_el2_tlbi_vmid)
FUNCTION_LABEL(el2_hvc_tlbi_vmid)
    // Switch to the guest.
    msr vttbr_el2, x0
    isb

    // Invalidate VMID. Based on ARM DEN 0024A, page 12-5.
    dsb ishst
    tlbi vmalls12e1is
    dsb ish
    isb

    // Switch to the host.
    msr vttbr_el2, xzr
    isb

    exception_return ZX_OK

// zx_status_t arm64_el2_resume(zx_paddr_t el2_state, zx_paddr_t vttbr);
FUNCTION(arm64_el2_resume)
    hvc #4
    ret
END_FUNCTION(arm64_el2_resume)
FUNCTION_LABEL(el2_hvc_resume)
    // Save El2State into tpidr_el2.
    msr tpidr_el2, x0
    mov x9, x0

    // Turn on virtualisation translation table to return to guest.
    msr vttbr_el2, x1
    isb

    // Invalidate all VMID TLB entries.
    //
    // TODO(abdulla): Only do this on the first-run.
    tlbi vmalle1
    isb

    // Enable guest traps, and ensure EL1 is arm64.
    movlit x10, HCR_EL2_VM | HCR_EL2_PTW | HCR_EL2_FMO | HCR_EL2_IMO | HCR_EL2_AMO | HCR_EL2_DC | \
                HCR_EL2_TWI | HCR_EL2_TWE | HCR_EL2_TSC | HCR_EL2_TVM | HCR_EL2_RW
    msr hcr_el2, x10
    isb

    host_state stp
    system_state str, HS_SYSTEM_STATE
    system_state ldr, GS_SYSTEM_STATE
    guest_state ldp
    guest_x9_state ldp, x9

    // Return to guest.
    eret

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
    host_state ldp

    // Disable guest traps, and ensure EL1 is arm64.
    movlit x9, HCR_EL2_RW
    msr hcr_el2, x9
    isb

    // Turn off virtualisation translation table to return to host.
    msr vttbr_el2, xzr
    isb

    // Return to host.
    ldr x0, [sp, #8]
    pop_stack
    eret

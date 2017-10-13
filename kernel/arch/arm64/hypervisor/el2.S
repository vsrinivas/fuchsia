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
#define HCR_EL2_TWI                     (1 << 13)
#define HCR_EL2_TWE                     (1 << 14)
#define HCR_EL2_TSC                     (1 << 19)
#define HCR_EL2_TGE                     (1 << 27)
#define HCR_EL2_RW                      (1 << 31)

#define SCTLR_EL2_M                     (1 << 0)
#define SCTLR_EL2_A                     (1 << 1)
#define SCTLR_EL2_C                     (1 << 2)
#define SCTLR_EL2_SA                    (1 << 3)
#define SCTLR_EL2_I                     (1 << 12)

#define VTCR_EL2_PS_SHIFT               16

// NOTE(abdulla): This excludes the top bit, as it is too large for VTCR_EL2.PS.
#define ID_AA64MMFR0_EL1_PARANGE_MASK   0x07

#define HVC_PSCI_INDEX                  0
#define HVC_ON_INDEX                    1
#define HVC_MAX_INDEX                   2

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
    \inst x19, x20, [x9, HS_X18]
    \inst x21, x22, [x9, HS_X18 + 16]
    \inst x23, x24, [x9, HS_X18 + 32]
    \inst x25, x26, [x9, HS_X18 + 48]
    \inst x27, x28, [x9, HS_X18 + 64]
    \inst x29, x30, [x9, HS_X18 + 80]
.ifc "\inst", "ldp"
    ldr x18, [x9, HS_X18 + 96]
.else
    str x18, [x9, HS_X18 + 96]
.endif
.endm

.macro guest_state inst
    \inst x0, x1, [x9, GS_X0]
    \inst x2, x3, [x9, GS_X0 + 16]
    \inst x4, x5, [x9, GS_X0 + 32]
    \inst x6, x7, [x9, GS_X0 + 48]
    \inst x8, x10, [x9, GS_X0 + 64]
    \inst x11, x12, [x9, GS_X0 + 80]
    \inst x13, x14, [x9, GS_X0 + 96]
    \inst x15, x16, [x9, GS_X0 + 112]
    \inst x17, x18, [x9, GS_X0 + 128]
    \inst x19, x20, [x9, GS_X0 + 144]
    \inst x21, x22, [x9, GS_X0 + 160]
    \inst x23, x24, [x9, GS_X0 + 176]
    \inst x25, x26, [x9, GS_X0 + 192]
    \inst x27, x28, [x9, GS_X0 + 208]
    \inst x29, x30, [x9, GS_X0 + 224]
.endm

.macro exception_return literal
    movlit x0, \literal
    eret
.endm

.macro pop_stack
    add sp, sp, 16
.endm

.macro entry_init
.align 7
    // Check ESR_EL2.EC to determine what caused the exception.
    mrs x9, esr_el2
    lsr x10, x9, #ESR_EL2_EC_SHIFT
    cmp x10, #ESR_EL2_EC_HVC
    b.ne invalid_exception

    and x10, x9, #ESR_EL2_ISS_MASK
    cmp x10, #HVC_PSCI_INDEX
    b.eq smc_psci
    cmp x10, #HVC_ON_INDEX
    b.ne invalid_exception

    b el2_hvc_on

invalid_exception:
    exception_return ZX_ERR_INVALID_ARGS
.endm

.macro entry_exec
.align 7
    // We push X9 onto the stack so we have one scratch register. We only use
    // X9 here, so that we don't accidentally trample the guest state.
    stp x9, xzr, [sp, #-16]!

    // Check VTTBR_EL2 to determine whether the exception came from the guest or
    // from the host.
    mrs x9, vttbr_el2
    cbnz x9, el2_guest_handler

    // The exception came from the host, so there is no guest state to preserve.
    pop_stack

    // Check ESR_EL2.EC to determine what caused the exception.
    //
    // NOTE(abdulla): We preserve ESR_EL2 in X9 so that we can reuse it later.
    mrs x9, esr_el2
    lsr x10, x9, #ESR_EL2_EC_SHIFT
    cmp x10, #ESR_EL2_EC_HVC
    b.eq el2_hvc_handler

    // We are here because the exception class is unhandled, so return failure.
    exception_return ZX_ERR_INVALID_ARGS
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
    entry_exec
    entry_invalid_exception
    entry_invalid_exception
    entry_invalid_exception

    /* exceptions from lower EL, running arm32 */
    entry_invalid_exception
    entry_invalid_exception
    entry_invalid_exception
    entry_invalid_exception

// zx_status_t arm64_el2_on(zx_paddr_t stack_top);
//
// |stack_top| must point to the physical address of a contiguous stack.
FUNCTION(arm64_el2_on)
    hvc #1
    ret
END_FUNCTION(arm64_el2_on)
FUNCTION_LABEL(el2_hvc_on)
    mov sp, x0

    // Load PARange from ID_AA64MMFR0_EL1.
    mrs x11, id_aa64mmfr0_el1
    and x11, x11, #ID_AA64MMFR0_EL1_PARANGE_MASK

    // Setup the virtualisation translation control.
    movlit x9, MMU_VTCR_EL2_FLAGS
    // Combine MMU_VTCR_EL2_FLAGS with VTCR_EL2.PS.
    lsl x10, x11, #VTCR_EL2_PS_SHIFT
    orr x9, x9, x10
    msr vtcr_el2, x9

    // Disable MMU, but enable I-cache, D-cache, and all alignment checking.
    mrs x9, sctlr_el2
    movlit x10, SCTLR_EL2_A | SCTLR_EL2_C | SCTLR_EL2_SA | SCTLR_EL2_I
    orr x9, x9, x10
    bic x9, x9, #SCTLR_EL2_M
    msr sctlr_el2, x9
    isb

    // Invalidate all EL2 TLB entries and synchronize I-cache and D-cache.
    tlbi alle2
    isb
    dsb sy

    // Setup the exec vector table for EL2.
    mrs x9, vbar_el2
    add x9, x9, #VECTOR_TABLE_SIZE
    msr vbar_el2, x9
    isb

    exception_return ZX_OK

FUNCTION_LABEL(el2_hvc_handler)
    // Check ESR_EL2.ICC to determine whether the HVC index is in range.
    //
    // NOTE(abdulla): We reuse ESR_EL2 that was preserved in X9.
    and x10, x9, #ESR_EL2_ISS_MASK
    cmp x10, #HVC_MAX_INDEX
    b.gt out_of_range

    // Branch to the jump table.
    lsl x10, x10, #2
    adr x9, table
    add x9, x9, x10
    br x9

table:
    b el2_hvc_psci
    b el2_hvc_off
    b el2_hvc_resume

out_of_range:
    exception_return ZX_ERR_OUT_OF_RANGE

FUNCTION_LABEL(el2_hvc_psci)
    pop_stack
smc_psci:
    smc #0
    eret

// zx_status_t arm64_el2_off();
FUNCTION(arm64_el2_off)
    hvc #1
    ret
END_FUNCTION(arm64_el2_off)
FUNCTION_LABEL(el2_hvc_off)
    // Setup the init vector table for EL2.
    mrs x9, vbar_el2
    sub x9, x9, #VECTOR_TABLE_SIZE
    msr vbar_el2, x9
    isb

    exception_return ZX_OK

// zx_status_t arm64_el2_resume(zx_paddr_t el2_state, zx_paddr_t vttbr);
FUNCTION(arm64_el2_resume)
    hvc #2
    ret
END_FUNCTION(arm64_el2_resume)
FUNCTION_LABEL(el2_hvc_resume)
    // Save El2State into tpidr_el2.
    msr tpidr_el2, x0
    mov x9, x0

    // Turn on virtualisation translation table to return to guest.
    msr vttbr_el2, x1
    // Enable guest traps, and ensure EL1 is arm64.
    movlit x10, HCR_EL2_VM | HCR_EL2_TWI | HCR_EL2_TWE | HCR_EL2_TSC | HCR_EL2_TGE | HCR_EL2_RW
    msr hcr_el2, x10
    isb

    host_state stp
    system_state str, HS_SYSTEM_STATE
    system_state ldr, GS_SYSTEM_STATE
    guest_state ldp

    // Return to guest.
    eret

FUNCTION_LABEL(el2_guest_handler)
    // Load El2State from tpidr_el2.
    mrs x9, tpidr_el2

    guest_state stp
    system_state str, GS_SYSTEM_STATE
    system_state ldr, HS_SYSTEM_STATE
    host_state ldp

    // Save X9 to El2State and pop the stack.
    ldr x0, [sp]
    str x0, [x9, GS_X0 + 240]
    pop_stack

    // Save ESR_EL2 and HPFAR_EL2 to El2State.
    mrs x10, esr_el2
    str x10, [x9, GS_ESR_EL2]
    mrs x10, hpfar_el2
    // This is not described well in the manual, but HPFAR_EL2 does not contain
    // the lower 8 bits of the IPA, so it must be shifted.
    lsl x10, x10, #8
    str x10, [x9, GS_HPFAR_EL2]

    // Disable guest traps, and ensure EL1 is arm64.
    movlit x9, HCR_EL2_RW
    msr hcr_el2, x9
    // Turn off virtualisation translation table to return to host.
    msr vttbr_el2, xzr
    isb

    // Return to host.
    exception_return ZX_OK

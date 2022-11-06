// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64/hypervisor/el2_state.h>
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

#define MDCR_EL2_TDOSA                  BIT_32(10)

.section .text.el2,"ax",@progbits
.align 12

// Temporary registers to use.
reg_xs  .req x9
reg_xt  .req x10
reg_xa  .req x11

.macro fp_state inst, off
    add reg_xs, reg_xs, \off
    \inst q0, q1, [reg_xs, FS_Q(0)]
    \inst q2, q3, [reg_xs, FS_Q(2)]
    \inst q4, q5, [reg_xs, FS_Q(4)]
    \inst q6, q7, [reg_xs, FS_Q(6)]
    \inst q8, q9, [reg_xs, FS_Q(8)]
    \inst q10, q11, [reg_xs, FS_Q(10)]
    \inst q12, q13, [reg_xs, FS_Q(12)]
    \inst q14, q15, [reg_xs, FS_Q(14)]
    \inst q16, q17, [reg_xs, FS_Q(16)]
    \inst q18, q19, [reg_xs, FS_Q(18)]
    \inst q20, q21, [reg_xs, FS_Q(20)]
    \inst q22, q23, [reg_xs, FS_Q(22)]
    \inst q24, q25, [reg_xs, FS_Q(24)]
    \inst q26, q27, [reg_xs, FS_Q(26)]
    \inst q28, q29, [reg_xs, FS_Q(28)]
    \inst q30, q31, [reg_xs, FS_Q(30)]
.ifc "\inst", "ldp"
    ldr reg_xt, [reg_xs, FS_FPSR]
    msr fpsr, reg_xt
    ldr reg_xt, [reg_xs, FS_FPCR]
    msr fpcr, reg_xt
.else
    mrs reg_xt, fpsr
    str reg_xt, [reg_xs, FS_FPSR]
    mrs reg_xt, fpcr
    str reg_xt, [reg_xs, FS_FPCR]
.endif
    sub reg_xs, reg_xs, \off
.endm

.macro system_register inst, off, sysreg
.ifc "\inst", "ldr"
    ldr reg_xa, [reg_xt, \off]
    msr \sysreg, reg_xa
.else
    mrs reg_xa, \sysreg
    str reg_xa, [reg_xt, \off]
.endif
.endm

.macro system_state inst, off
    add reg_xt, reg_xs, \off
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
.endm

.macro host_state inst, off
    add reg_xt, reg_xs, \off
    \inst x15, x18, [reg_xt, HS_X(0)]
    \inst x19, x20, [reg_xt, HS_X(2)]
    \inst x21, x22, [reg_xt, HS_X(4)]
    \inst x23, x24, [reg_xt, HS_X(6)]
    \inst x25, x26, [reg_xt, HS_X(8)]
    \inst x27, x28, [reg_xt, HS_X(10)]
    \inst x29, x30, [reg_xt, HS_X(12)]
.endm

.macro guest_state inst
    \inst x0, x1, [reg_xs, GS_X(0)]
    \inst x2, x3, [reg_xs, GS_X(2)]
    \inst x4, x5, [reg_xs, GS_X(4)]
    \inst x6, x7, [reg_xs, GS_X(6)]
    \inst x10, x11, [reg_xs, GS_X(10)]
    \inst x12, x13, [reg_xs, GS_X(12)]
    \inst x14, x15, [reg_xs, GS_X(14)]
    \inst x16, x17, [reg_xs, GS_X(16)]
    \inst x18, x19, [reg_xs, GS_X(18)]
    \inst x20, x21, [reg_xs, GS_X(20)]
    \inst x22, x23, [reg_xs, GS_X(22)]
    \inst x24, x25, [reg_xs, GS_X(24)]
    \inst x26, x27, [reg_xs, GS_X(26)]
    \inst x28, x29, [reg_xs, GS_X(28)]
.ifc "\inst", "ldp"
    ldr x30, [reg_xs, GS_X(30)]
.else
    str x30, [reg_xs, GS_X(30)]
.endif
.endm

.macro guest_xs_state inst, reg
    \inst x8, \reg, [reg_xs, GS_X(8)]
.endm

.macro guest_enter_state
    ldr reg_xt, [reg_xs, GS_CNTV_CVAL_EL0]
    msr cntv_cval_el0, reg_xt
    ldr reg_xt, [reg_xs, GS_CNTV_CTL_EL0]
    msr cntv_ctl_el0, reg_xt
    ldr reg_xt, [reg_xs, GS_VMPIDR_EL2]
    msr vmpidr_el2, reg_xt
.endm

.macro guest_exit_state
    mrs reg_xt, cntv_ctl_el0
    str reg_xt, [reg_xs, GS_CNTV_CTL_EL0]
    mrs reg_xt, cntv_cval_el0
    str reg_xt, [reg_xs, GS_CNTV_CVAL_EL0]
    mrs reg_xt, esr_el2
    str reg_xt, [reg_xs, GS_ESR_EL2]
    mrs reg_xt, far_el2
    str reg_xt, [reg_xs, GS_FAR_EL2]
    mrs reg_xt, hpfar_el2
    // This is not described well in the manual, but HPFAR_EL2 does not contain
    // the lower 8 bits of the IPA, so it must be shifted.
    lsl reg_xt, reg_xt, 8
    str reg_xt, [reg_xs, GS_HPFAR_EL2]
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
    SPECULATION_POSTFENCE
.endm

.macro pop_stack
    add sp, sp, 16
.endm

.macro hvc_jump table size
    mrs reg_xs, esr_el2

    // Check ESR_EL2.EC to determine what caused the exception.
    lsr reg_xt, reg_xs, ESR_EL2_EC_SHIFT
    cmp reg_xt, ESR_EL2_EC_HVC
    b.ne .Linvalid_args_for_\table

    // Check ESR_EL2.ICC to determine whether the HVC index is in range.
    and reg_xt, reg_xs, ESR_EL2_ISS_MASK
    cmp reg_xt, \size
    b.ge .Linvalid_args_for_\table

    // Branch to the jump table.
    adr reg_xs, \table
    add reg_xs, reg_xs, reg_xt, lsl 2
    br reg_xs

.Linvalid_args_for_\table:
    exception_return ZX_ERR_INVALID_ARGS
.endm

.macro guest_exit return_code
    // We push reg_xs onto the stack so we have one scratch register. We only
    // use reg_xs here, so that we don't accidentally trample the guest state.
    str reg_xs, [sp, -16]!
    mov reg_xs, \return_code
    str reg_xs, [sp, 8]
.endm

.macro entry_init
.align 7
    hvc_jump .Linit_table 9
.Linit_table:
    b el2_hvc_psci        // HVC 0:      psci
    b el2_hvc_mexec       // HVC 1:      mexec
    b el2_hvc_tlbi        // HVC 2:      tlbi
    b el2_hvc_on          // HVC 3:      on
    b el2_hvc_bad_state   // HVC 4: (**) off        (disabled)
    b el2_hvc_bad_state   // HVC 5: (**) enter      (disabled)
    b el2_hvc_bad_state   // HVC 6: (**) gich_state (disabled)
    b el2_hvc_bad_state   // HVC 7: (**) gich_vtr   (disabled)
    b el2_hvc_tlbi_el2    // HVC 8:      tlbi EL2
.endm

.macro entry_sync return_code
.align 7
    guest_exit \return_code
    // Check VTTBR_EL2 to determine whether the exception came from the guest or
    // from the host.
    mrs reg_xs, vttbr_el2
    cbnz reg_xs, el2_guest_exit_or_fp_enter

    // The exception came from the host, so there is no guest state to preserve.
    pop_stack

    // If we got to here, the exception came from the host or EL2.
    // Continue execution through a jump table based on the HVC index.
    hvc_jump .Lsync_table 9
.Lsync_table:
    b el2_hvc_psci        // HVC 0:      psci
    b el2_hvc_mexec       // HVC 1:      mexec
    b el2_hvc_tlbi        // HVC 2:      tlbi
    b el2_hvc_bad_state   // HVC 3: (**) on        (disabled)
    b el2_hvc_off         // HVC 4:      off
    b el2_hvc_enter       // HVC 5:      enter
    b el2_hvc_gich_state  // HVC 6:      gich_state
    b el2_hvc_gich_vtr    // HVC 7:      gich_vtr
    b el2_hvc_tlbi_el2    // HVC 8:      tlbi EL2
.endm

.macro entry_async return_code
.align 7
    guest_exit \return_code
    b el2_guest_exit
.endm

.macro entry_invalid_exception
.align 7
    // If we got to here, the exception came from the host or EL2.
    //
    // Hang, relying on the lockup detector or watchdog to kick in.
    1:
    b 1b
.endm

// We have two vector tables that we switch between, init and exec. The reason
// is that we need to use the stack to temporarily save registers when we exit
// from a guest. However, that stack may have not been set up, and therefore we
// can not unconditionally use it. We use the init vector table to set up the
// stack and hypervisor state, and we use the exec vector table to maintain
// execution of the hypervisor.

.align 11
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

.align 11
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
    entry_sync  ZX_OK          // Exception
    entry_async ZX_ERR_NEXT    // IRQ
    entry_invalid_exception
    entry_async ZX_OK          // SError

    /* exceptions from lower EL, running arm32 */
    entry_invalid_exception
    entry_invalid_exception
    entry_invalid_exception
    entry_invalid_exception

// zx_status_t arm64_el2_on(zx_paddr_t ttbr0, zx_paddr_t stack_top, uint64_t tcr, uint64_t vtcr);
//
// |stack_top| must point to the physical address of a contiguous stack.
FUNCTION(arm64_el2_on)
    hvc 3
    ret
END_FUNCTION(arm64_el2_on)
FUNCTION_LABEL(el2_hvc_on)
    // Setup the EL2 translation table.
    msr ttbr0_el2, x0

    // Setup the EL2 stack pointer.
    mov sp, x1

    // Setup the EL2 translation control.
    msr tcr_el2, x2

    // Setup the virtualisation translation control.
    msr vtcr_el2, x3

    // Setup the EL2 memory attributes.
    movlit reg_xs, MMU_MAIR_VAL
    msr mair_el2, reg_xs
    isb

    // Invalidate all EL2 TLB entries.
    tlbi alle2
    dsb sy
    isb

    // Enable the MMU, I-cache, D-cache, and stack alignment checking.
    movlit reg_xs, SCTLR_ELX_M | SCTLR_ELX_C | SCTLR_ELX_SA | SCTLR_ELX_I | SCTLR_EL2_RES1
    msr sctlr_el2, reg_xs
    isb

    // Setup the exec vector table for EL2.
    adr_global reg_xs, arm64_el2_exec_table
    msr vbar_el2, reg_xs
    isb

    exception_return ZX_OK

FUNCTION_LABEL(el2_hvc_psci)
    smc 0
    eret
    SPECULATION_POSTFENCE

FUNCTION_LABEL(el2_hvc_mexec)
    br x0

// zx_status_t arm64_el2_tlbi_ipa(uint64_t vttbr, zx_vaddr_t addr, bool terminal);
FUNCTION(arm64_el2_tlbi_ipa)
    mov reg_xa, 0
    hvc 2
    ret
END_FUNCTION(arm64_el2_tlbi_ipa)

// zx_status_t arm64_el2_tlbi_vmid(uint64_t vttbr);
FUNCTION(arm64_el2_tlbi_vmid)
    mov reg_xa, 1
    hvc 2
    ret
END_FUNCTION(arm64_el2_tlbi_vmid)

FUNCTION_LABEL(el2_hvc_tlbi)
    switch_to_guest

    cbz reg_xa, el2_hvc_tlbi_ipa
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

// zx_status_t arm64_el2_off();
FUNCTION(arm64_el2_off)
    hvc 4
    ret
END_FUNCTION(arm64_el2_off)
FUNCTION_LABEL(el2_hvc_off)
    // Disable the MMU, but enable I-cache, D-cache, and stack alignment checking.
    movlit reg_xs, SCTLR_ELX_C | SCTLR_ELX_SA | SCTLR_ELX_I | SCTLR_EL2_RES1
    msr sctlr_el2, reg_xs
    isb

    // Invalidate all EL2 TLB entries.
    tlbi alle2
    dsb sy
    isb

    // Setup the init vector table for EL2.
    adr_global reg_xs, arm64_el2_init_table
    msr vbar_el2, reg_xs
    isb

    exception_return ZX_OK

// zx_status_t arm64_el2_enter(uint64_t vttbr, zx_paddr_t state, uint64_t hcr);
FUNCTION(arm64_el2_enter)
    hvc 5
    ret
END_FUNCTION(arm64_el2_enter)
FUNCTION_LABEL(el2_hvc_enter)
    switch_to_guest

    // Save El2State into tpidr_el2.
    msr tpidr_el2, x1
    mov reg_xs, x1

    // If the guest is being run for the first time, invalidate all VMID TLB
    // entries in case the VMID has been used previously.
    ldr reg_xt, [reg_xs, ES_RESUME]
    cbnz reg_xt, .Lresume
    tlbi vmalle1
    mov reg_xt, 1
    str reg_xt, [reg_xs, ES_RESUME]
    dsb nshst

.Lresume:
    // Set the hypervisor control register.
    msr hcr_el2, x2

    // Disable access to physical timer.
    mov reg_xt, CNTHCTL_EL2_EL1PCTEN
    msr cnthctl_el2, reg_xt

    // Trap any accesses to debug related registers in the guest
    mov reg_xt, MDCR_EL2_TDOSA
    msr mdcr_el2, reg_xt

    // Enable floating-point traps.
    movlit reg_xt, CPTR_EL2_RES1 | CPTR_EL2_TFP
    msr cptr_el2, reg_xt
    isb

    host_state stp, HS_XREGS
    system_state str, HS_SYSTEM_STATE
    guest_enter_state
    system_state ldr, GS_SYSTEM_STATE
    guest_state ldp
    guest_xs_state ldp, reg_xs

    // Return to guest.
    eret
    SPECULATION_POSTFENCE

FUNCTION_LABEL(el2_guest_exit_or_fp_enter)
    // Check ESR_EL2.EC to determine whether the exception was due to a
    // floating-point trap.
    mrs reg_xs, esr_el2
    lsr reg_xs, reg_xs, ESR_EL2_EC_SHIFT
    cmp reg_xs, ESR_EL2_EC_FP
    b.eq el2_fp_enter

FUNCTION_LABEL(el2_guest_exit)
    // Load El2State from tpidr_el2.
    mrs reg_xs, tpidr_el2

    guest_state stp
    // Load reg_xs from the stack, and save it in GuestState.
    ldr reg_xt, [sp]
    guest_xs_state stp, reg_xt
    system_state str, GS_SYSTEM_STATE
    guest_exit_state
    system_state ldr, HS_SYSTEM_STATE
    host_state ldp, HS_XREGS

    mrs reg_xt, cptr_el2
    tbnz reg_xt, CPTR_EL2_TFP_SHIFT, .Lfp_untrap

    // Restore floating-point state if it was modified.
    fp_state stp, GS_FP_STATE
    fp_state ldp, HS_FP_STATE
    b .Lfp_done

.Lfp_untrap:
    // Disable floating-point traps.
    mov reg_xt, CPTR_EL2_RES1
    msr cptr_el2, reg_xt

.Lfp_done:
    // Disable virtual timer set by guest, and enable access to physical timer.
    msr cntv_ctl_el0, xzr
    mov reg_xt, CNTHCTL_EL2_EL1PCTEN | CNTHCTL_EL2_EL1PCEN
    msr cnthctl_el2, reg_xt

    // Don't trap debug register access to EL2.
    msr mdcr_el2, xzr

    // Disable guest traps, and ensure EL1 is arm64.
    mov reg_xt, HCR_EL2_RW
    msr hcr_el2, reg_xt
    isb

    switch_to_host

    // Return to host.
    ldr x0, [sp, 8]
    pop_stack
    eret
    SPECULATION_POSTFENCE

FUNCTION_LABEL(el2_fp_enter)
    // Save reg_xt so we have an extra register to swap floating-point state.
    // We're returning to the guest so we don't need the return code in [sp, 8].
    str reg_xt, [sp, 8]

    // Disable floating-point traps and reset exception syndrome.
    mov reg_xs, CPTR_EL2_RES1
    msr cptr_el2, reg_xs
    msr esr_el2, xzr
    isb

    // Load El2State from tpidr_el2.
    mrs reg_xs, tpidr_el2

    fp_state stp, HS_FP_STATE
    fp_state ldp, GS_FP_STATE

    // Return to guest.
    ldp reg_xs, reg_xt, [sp], 16
    eret
    SPECULATION_POSTFENCE

// zx_status_t arm64_el2_tlbi_el2();
FUNCTION(arm64_el2_tlbi_el2)
    hvc 8
    ret
END_FUNCTION(arm64_el2_tlbi_el2)

FUNCTION_LABEL(el2_hvc_tlbi_el2)
    // Invalidate all EL2 TLB entries.
    tlbi alle2
    dsb sy
    isb

    exception_return ZX_OK

FUNCTION_LABEL(el2_hvc_bad_state)
    // Simply returns the ZX_ERR_BAD_STATE error code.
    exception_return ZX_ERR_BAD_STATE

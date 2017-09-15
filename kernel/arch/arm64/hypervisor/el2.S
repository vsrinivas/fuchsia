// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64/mmu.h>
#include <arch/asm_macros.h>
#include <asm.h>
#include <zircon/errors.h>

#define ESR_EL2_EC_MASK     0xfc000000
#define ESR_EL2_ISS_MASK    0x01ffffff
#define SCTLR_EL2_M         (1 << 0)
#define SCTLR_EL2_C         (1 << 2)
#define SCTLR_EL2_I         (1 << 12)
#define HVC_MAX_INDEX       2

.section .text.el2,"ax",@progbits
.align 12

.macro invalid_exception
    // TODO(abdulla): Check VMID from VTTBR_EL2. ERET to host with error. If
    // VMID was not 0, terminate guest.
    //
    // In the meantime, generate a breakpoint exception.
    brk #0
    eret
.endm

.macro sync_exception
    mrs x10, esr_el2
    and x10, x10, #ESR_EL2_ISS_MASK
    cmp x10, #HVC_MAX_INDEX
    b.ge out_of_range

    lsl x10, x10, #2
    adr x9, table
    add x9, x9, x10
    br x9

table:
    b el2_on
    b el2_off

out_of_range:
    movlit x0, ZX_ERR_OUT_OF_RANGE
    eret
.endm

FUNCTION_LABEL(arm64_el2_exception_base)

/* exceptions from current EL, using SP0 */
.org 0x000
LOCAL_FUNCTION(arm64_el2_sync_exc_current_el_SP0)
    invalid_exception
END_FUNCTION(arm64_el2_sync_exc_current_el_SP0)

.org 0x080
LOCAL_FUNCTION(arm64_el2_irq_current_el_SP0)
    invalid_exception
END_FUNCTION(arm64_el2_irq_current_el_SP0)

.org 0x100
LOCAL_FUNCTION(arm64_el2_fiq_current_el_SP0)
    invalid_exception
END_FUNCTION(arm64_el2_fiq_current_el_SP0)

.org 0x180
LOCAL_FUNCTION(arm64_el2_err_exc_current_el_SP0)
    invalid_exception
END_FUNCTION(arm64_el2_err_exc_current_el_SP0)

/* exceptions from current EL, using SPx */
.org 0x200
LOCAL_FUNCTION(arm64_el2_sync_exc_current_el_SPx)
    invalid_exception
END_FUNCTION(arm64_el2_sync_exc_current_el_SPx)

.org 0x280
LOCAL_FUNCTION(arm64_el2_irq_current_el_SPx)
    invalid_exception
END_FUNCTION(arm64_el2_irq_current_el_SPx)

.org 0x300
LOCAL_FUNCTION(arm64_el2_fiq_current_el_SPx)
    invalid_exception
END_FUNCTION(arm64_el2_fiq_current_el_SPx)

.org 0x380
LOCAL_FUNCTION(arm64_el2_err_exc_current_el_SPx)
    invalid_exception
END_FUNCTION(arm64_el2_err_exc_current_el_SPx)

/* exceptions from lower EL, running arm64 */
.org 0x400
LOCAL_FUNCTION(arm64_el2_sync_exc_lower_el_64)
    sync_exception
END_FUNCTION(arm64_el2_sync_exc_lower_el_64)

.org 0x480
LOCAL_FUNCTION(arm64_el2_irq_lower_el_64)
    invalid_exception
END_FUNCTION(arm64_el2_irq_lower_el_64)

.org 0x500
LOCAL_FUNCTION(arm64_el2_fiq_lower_el_64)
    invalid_exception
END_FUNCTION(arm64_el2_fiq_lower_el_64)

.org 0x580
LOCAL_FUNCTION(arm64_el2_err_exc_lower_el_64)
    invalid_exception
END_FUNCTION(arm64_el2_err_exc_lower_el_64)

/* exceptions from lower EL, running arm32 */
.org 0x600
LOCAL_FUNCTION(arm64_el2_sync_exc_lower_el_32)
    invalid_exception
END_FUNCTION(arm64_el2_sync_exc_lower_el_32)

.org 0x680
LOCAL_FUNCTION(arm64_el2_irq_lower_el_32)
    invalid_exception
END_FUNCTION(arm64_el2_irq_lower_el_32)

.org 0x700
LOCAL_FUNCTION(arm64_el2_fiq_lower_el_32)
    invalid_exception
END_FUNCTION(arm64_el2_fiq_lower_el_32)

.org 0x780
LOCAL_FUNCTION(arm64_el2_err_exc_lower_el_32)
    invalid_exception
END_FUNCTION(arm64_el2_err_exc_lower_el_32)

// zx_status_t arm64_el2_on(zx_paddr_t stack_top);
//
// |stack_top| must point to the physical address of a contiguous stack.
FUNCTION(arm64_el2_on)
    hvc #0
    ret
END_FUNCTION(arm64_el2_on)
LOCAL_FUNCTION(el2_on)
    mov sp, x0

    // Setup memory attributes and translation controls.
    movlit x9, MMU_MAIR_VAL
    msr mair_el2, x9
    movlit x9, MMU_TCR_FLAGS_IDENT
    msr tcr_el2, x9
    isb

    // Setup translation table to point at the trampoline, which contains an
    // identity mapped translation table.
    adr_global x9, tt_trampoline
    msr ttbr0_el2, x9
    isb

    // Invalidate all EL2 TLB entries and flush I-cache and D-cache.
    tlbi alle2
    isb
    dsb sy

    // Enable the MMU for EL2, with I-cache and D-cache.
    mrs x9, sctlr_el2
    movlit x10, SCTLR_EL2_M | SCTLR_EL2_C | SCTLR_EL2_I
    orr x9, x9, x10
    msr sctlr_el2, x9
    isb

    mov x0, #ZX_OK
    eret
END_FUNCTION(el2_on)

// zx_status_t arm64_el2_off();
FUNCTION(arm64_el2_off)
    hvc #1
    ret
END_FUNCTION(arm64_el2_off)
LOCAL_FUNCTION(el2_off)
    // Disable the MMU for EL2.
    mrs x9, sctlr_el2
    bic x9, x9, #SCTLR_EL2_M
    msr sctlr_el2, x9
    isb

    mov x0, #ZX_OK
    eret
END_FUNCTION(el2_off)

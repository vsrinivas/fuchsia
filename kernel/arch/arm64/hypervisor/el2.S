// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <arch/arm64/mmu.h>
#include <arch/asm_macros.h>
#include <asm.h>
#include <magenta/errors.h>

#define ESR_EL2_EC_MASK     0xfc000000
#define ESR_EL2_ISS_MASK    0x01ffffff
#define HVC_MAX_INDEX       1

.section .text.el2,"ax",@progbits
.align 12

// EL2 functions
LOCAL_FUNCTION(el2_set_stack)
    mov sp, x0
    mov x0, #MX_OK
    eret
END_FUNCTION(el2_set_stack)

.section .text.boot.vectab.el2,"ax",@progbits
.align 12

.macro invalid_exception
    // TODO(abdulla): Check VMID from VTTBR_EL2. ERET to host with error. If
    // VMID was not 0, terminate guest.
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
    b el2_set_stack

out_of_range:
    mov x0, MX_ERR_OUT_OF_RANGE
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

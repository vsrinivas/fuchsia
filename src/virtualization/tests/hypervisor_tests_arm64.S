// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "hypervisor_tests_constants.h"

.set ICC_IAR1_EL1,              S3_0_C12_C12_0

.set CPACR_EL1_FPEN_NO_TRAP,    (3 << 20)

// See ARM ARM Table D1.10.2 for more details.
.set CURRENT_EL_EL0_BASE,       0x000
.set CURRENT_EL_ELx_BASE,       0x200
.set LOWER_EL_ARCH64_BASE,      0x400
.set LOWER_EL_ARCH32_BASE,      0x600
.set SYNC_EXC_OFFSET,           0x000
.set IRQ_EXC_OFFSET,            0x080
.set FIQ_EXC_OFFSET,            0x100
.set SERROR_EXC_OFFSET,         0x180

.set SPSR_DAIF_AARCH64,         0x3c0
.set SPSR_AIFM_AARCH32,         0x1d0

.set GICC_BASE,                 0x800001000
.set GICC_IAR_OFFSET,           0xc

.set PSCI64_SYSTEM_OFF,         0x84000008

.macro movlit reg, literal
mov \reg, #((\literal) & 0xffff)
.ifne (((\literal) >> 16) & 0xffff)
movk \reg, #(((\literal) >> 16) & 0xffff), lsl #16
.endif
.ifne (((\literal) >> 32) & 0xffff)
movk \reg, #(((\literal) >> 32) & 0xffff), lsl #32
.endif
.ifne (((\literal) >> 48) & 0xffff)
movk \reg, #(((\literal) >> 48) & 0xffff), lsl #48
.endif
.endm

// Define new function 'name'.
// Local label '1:' used by other macros to compute offsets.
.macro FUNCTION name
    .global \name
    .type \name, STT_FUNC
    \name:
    1:
.endm

// Adjusts current text location to position at a specific exception handler.
.macro exception_vector base offset
    2:
    .skip (\base + \offset) - (2b - 1b)
.endm

// Signals test completion by writing 0 to EXIT_TEST_ADDR.
.macro test_complete arch=aarch64
    .ifc \arch, aarch64
        mov x0, EXIT_TEST_ADDR
        str xzr, [x0]
    .else
        .ifc \arch, aarch32
            .word 0xE3A01000    // aarch32: movw r1, 0
            .word 0xE30F0000    // aarch32: movw r0, [lower]EXIT_TEST_ADDR
            .word 0xE34000FF    // aarch32: movw r0, [upper]EXIT_TEST_ADDR
            .word 0xE5801000    // aarch32: str  r0, [r1]
        .else
            .error "Unsupported architecture"
        .endif
    .endif
.endm

// Drop exception level from EL1 to EL0 and, if requested, switch execution
// state.
.macro drop_to_el0 arch=aarch64
    .ifc \arch, aarch64
        mov x0, SPSR_DAIF_AARCH64
    .else
        .ifc \arch, aarch32
            mov x0, SPSR_AIFM_AARCH32
        .else
            .error "Unsupported architecture"
        .endif
    .endif
    msr spsr_el1, x0
    isb

    mov x0, xzr
    add x0, x0, 2f - 1b // resume execution at label '2' ahead
    msr elr_el1, x0     // store exception link register
    eret                // resume at el0 using specified arch.
    2:
.endm

.text

// Test vcpu_resume.
FUNCTION vcpu_resume_start
    test_complete
FUNCTION vcpu_resume_end

// Test vcpu_read_state and vcpu_write_state.
FUNCTION vcpu_read_write_state_start
    add x1, x1, #1
    add x2, x2, #2
    add x3, x3, #3
    add x4, x4, #4
    add x5, x5, #5
    add x6, x6, #6
    add x7, x7, #7
    add x8, x8, #8
    add x9, x9, #9
    add x10, x10, #10
    add x11, x11, #11
    add x12, x12, #12
    add x13, x13, #13
    add x14, x14, #14
    add x15, x15, #15
    add x16, x16, #16
    add x17, x17, #17
    add x18, x18, #18
    add x19, x19, #19
    add x20, x20, #20
    add x21, x21, #21
    add x22, x22, #22
    add x23, x23, #23
    add x24, x24, #24
    add x25, x25, #25
    add x26, x26, #26
    add x27, x27, #27
    add x28, x28, #28
    add x29, x29, #29
    add x30, x30, #30

    add sp, sp, #64
    cmp sp, #128 // Set ZC bits of CPSR.

    test_complete
FUNCTION vcpu_read_write_state_end

// Test vcpu_interrupt.
FUNCTION vcpu_interrupt_start
    msr daifclr, #2
    b .

    exception_vector CURRENT_EL_ELx_BASE, IRQ_EXC_OFFSET
    test_complete
FUNCTION vcpu_interrupt_end

// Test guest_set_trap using a memory-based trap.
FUNCTION guest_set_trap_start
    mov x0, TRAP_ADDR
    str xzr, [x0]
    test_complete
FUNCTION guest_set_trap_end

// Test wfi instruction handling.
FUNCTION vcpu_wfi_start
    // Setup the virtual timer by:
    // 1. Setting the compare value to 0.
    // 2. Enabling the virtual timer.
    msr cntv_cval_el0, xzr
    mov x0, 1
    msr cntv_ctl_el0, x0

    wfi
    test_complete
FUNCTION vcpu_wfi_end

// Test wfi handling with pending interrupt in LR (GICv2).
FUNCTION vcpu_wfi_pending_interrupt_gicv2_start
    msr daifclr, #2
    b .

    exception_vector CURRENT_EL_ELx_BASE, IRQ_EXC_OFFSET
    movlit x0, GICC_BASE
    // Acknowledge interrupt by reading IAR.
    ldr w1, [x0, GICC_IAR_OFFSET]
    wfi
    test_complete
FUNCTION vcpu_wfi_pending_interrupt_gicv2_end

// Test wfi handling with pending interrupt in LR (GICv3).
FUNCTION vcpu_wfi_pending_interrupt_gicv3_start
    msr daifclr, #2
    b .

    exception_vector CURRENT_EL_ELx_BASE, IRQ_EXC_OFFSET
    movlit x0, GICC_BASE
    // Acknowledge interrupt by reading IAR.
    mrs x1, ICC_IAR1_EL1
    dsb sy
    wfi
    test_complete
FUNCTION vcpu_wfi_pending_interrupt_gicv3_end

// Test wfi instruction handling.
// Execution of WFI at EL0 on AARCH32 is propagated to EL1 / AARCH64.
FUNCTION vcpu_wfi_aarch32_start
    drop_to_el0 aarch32
    .word 0xE320F003         // aarch32: wfi

    // From ARM DDI 0487C.a, Section D10.2.45: Since a WFI can complete at any
    // time, even without a Wakeup event, the traps on WFI are not guaranteed to
    // be taken, even if the WFI is executed when there is no Wakeup event. The
    // only guarantee is that if the instruction does not complete in finite
    // time in the absence of a Wakeup event, the trap will be taken.
    //
    // We therefore complete the test at this point.
    test_complete aarch32

    exception_vector LOWER_EL_ARCH32_BASE, SYNC_EXC_OFFSET
    test_complete
FUNCTION vcpu_wfi_aarch32_end

// Test floating-point instruction handling with trapping between levels.
FUNCTION vcpu_fp_start
    drop_to_el0

    // Access vector registers.
    mov w0, 0xff
    dup v0.16b, w0
    test_complete

    // Handle EL1 floating-point trap.
    // This is interpreted as a Lower exception level (coming from EL0)
    // captured by AARCH64. See ARM ARM Table D1.10.2 for more details.
    exception_vector LOWER_EL_ARCH64_BASE, SYNC_EXC_OFFSET
    mov x0, CPACR_EL1_FPEN_NO_TRAP
    msr cpacr_el1, x0
    eret
FUNCTION vcpu_fp_end

// Test wfi instruction handling.
FUNCTION vcpu_fp_aarch32_start
    drop_to_el0 aarch32
    // Load double precision register d0 from address 0.
    // This should trigger floating point exception.
    .word 0xE3A00000         // aarch32: mov r0, 0
    .word 0xED900B00         // aarch32: vldr d0, [r0]
    test_complete aarch32

    exception_vector LOWER_EL_ARCH32_BASE, SYNC_EXC_OFFSET
    mov x0, CPACR_EL1_FPEN_NO_TRAP
    msr cpacr_el1, x0
    eret
FUNCTION vcpu_fp_aarch32_end

FUNCTION vcpu_psci_system_off_start
    movlit x0, PSCI64_SYSTEM_OFF
    smc #0
FUNCTION vcpu_psci_system_off_end

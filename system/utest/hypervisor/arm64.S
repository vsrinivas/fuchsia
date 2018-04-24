// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "constants_priv.h"

.set CPACR_EL1_FPEN_NO_TRAP, (3 << 20)

// See ARM ARM Table D1.10.2 for more details.
.set CURRENT_EL_EL0_BASE,    0x000
.set CURRENT_EL_ELx_BASE,    0x200
.set LOWER_EL_ARCH64_BASE,   0x400
.set LOWER_EL_ARCH32_BASE,   0x600
.set SYNC_EXC_OFFSET,        0x000
.set IRQ_EXC_OFFSET,         0x080
.set FIQ_EXC_OFFSET,         0x100
.set SERROR_EXC_OFFSET,      0x180

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

// Signal completion to test case.
.macro test_complete
    mov x0,  EXIT_TEST_ADDR
    str xzr, [x0]
.endm

// Drop exception level from EL1 to EL0
.macro drop_to_el0
    ldr w0, 2f
    msr elr_el1, x0
    eret
    2:
    .word 2b - 1b + 4
.endm

.text

// Test vcpu_resume.
FUNCTION vcpu_resume_start
    test_complete
FUNCTION vcpu_resume_end

// Test vcpu_interrupt.
FUNCTION vcpu_interrupt_start
    msr daifclr, #2
    b .

    exception_vector CURRENT_EL_ELx_BASE, IRQ_EXC_OFFSET
    test_complete
FUNCTION vcpu_interrupt_end

// Test wfi instruction handling.
FUNCTION vcpu_wfi_start
    wfi
    test_complete
FUNCTION vcpu_wfi_end

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

// Test guest_set_trap using a memory-based trap.
FUNCTION guest_set_trap_start
    mov x0, TRAP_ADDR
    str xzr, [x0]
    test_complete
FUNCTION guest_set_trap_end

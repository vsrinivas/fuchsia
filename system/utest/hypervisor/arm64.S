// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "constants_priv.h"

#define CPACR_EL1_FPEN_NO_TRAP (3 << 20)

.text

// Test vcpu_resume.
FUNCTION(vcpu_resume_start)
    mov x0, EXIT_TEST_ADDR
    str xzr, [x0]
FUNCTION(vcpu_resume_end)

// Test vcpu_interrupt.
FUNCTION(vcpu_interrupt_start)
    msr daifclr, #2
    b .

.skip 0x278 // 0x280 minus 2 instructions.
    mov x0, EXIT_TEST_ADDR
    str xzr, [x0]
FUNCTION(vcpu_interrupt_end)

// Test wfi instruction handling.
FUNCTION(vcpu_wfi_start)
    wfi
    mov x0, EXIT_TEST_ADDR
    str xzr, [x0]
FUNCTION(vcpu_wfi_end)

// Test floating-point instruction handling with trapping between levels.
FUNCTION(vcpu_fp_start)
    // Drop to EL0.
    mov x0, 3 /* instructions */ * 4 /* instruction length */
    msr elr_el1, x0
    eret

    // Access vector registers.
    mov w0, 0xff
    dup v0.16b, w0

    // Exit test.
    mov x0, EXIT_TEST_ADDR
    str xzr, [x0]

    // Handle EL1 floating-point trap.
.skip 0x3e4 // 0x400 minus 7 instructions.
    mov x0, CPACR_EL1_FPEN_NO_TRAP
    msr cpacr_el1, x0
    eret
FUNCTION(vcpu_fp_end)

// Test vcpu_read_state and vcpu_write_state.
FUNCTION(vcpu_read_write_state_start)
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

    mov x0, EXIT_TEST_ADDR
    str xzr, [x0]
FUNCTION(vcpu_read_write_state_end)

// Test guest_set_trap using a memory-based trap.
FUNCTION(guest_set_trap_start)
    mov x0, TRAP_ADDR
    str xzr, [x0]
    mov x0, EXIT_TEST_ADDR
    str xzr, [x0]
FUNCTION(guest_set_trap_end)

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/arch/cache.h>

#include "src/virtualization/tests/hypervisor/asm.h"
#include "src/virtualization/tests/hypervisor/constants.h"

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
.set PSCI64_SYSTEM_RESET,       0x84000009

// TCR_EL1 for configuring the MMU.
//
// [arm/v8]: D13.2.120 TCR_EL1, Translation Control Register (EL1)
.set TCR_EL1_TG0_4KIB,            (0 << 14)                // TTBR0 Granule size.
.set TCR_EL1_T0SZ,                ((64 - REGION_SIZE_BITS) << 0)  // TTBR0 region size
.set TCR_EL1_SH0_INNER_SHAREABLE, (3 << 12)                // TTBR0 Inner shareable.
.set TCR_EL1_ORGN0_WB_WA,         (1 << 10)                // TTBR0 Outer write-back/write-allocate
.set TCR_EL1_IRGN0_WB_WA,         (1 << 8)                 // TTBR0 Inner write-back/write-allocate

.set GUEST_TCR_EL1, ( \
  (TCR_EL1_TG0_4KIB | TCR_EL1_T0SZ | TCR_EL1_SH0_INNER_SHAREABLE | TCR_EL1_ORGN0_WB_WA | \
   TCR_EL1_IRGN0_WB_WA))

// Memory Attribute Indirection Register (MAIR)
//
// [arm/v8]: D13.2.95  MAIR_EL1, Memory Attribute Indirection Register, EL1
.set MAIR_ATTR_NORMAL_CACHED, 0xff  // Normal Memory (outer WB, inner WB)
.set GUEST_MAIR_EL1, (MAIR_ATTR_NORMAL_CACHED << 0)

// Bits for the SCTLR registers.
.set SCTLR_ELX_M,  (1 << 0)   // MMU enabled.
.set SCTLR_ELX_C,  (1 << 2)   // Data cache enabled.
.set SCTLR_ELX_I,  (1 << 12)  // Instruction cache enabled.

.set MEMORY_TEST_BYTES,         (2 * 1024 * 1024)  // Region size to read/write for memory tests.

// Move a constant to a register, emitting multiple `mov` operations if required.
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

// Architecture-specific callbacks called by the FUNCTION macro.
.macro arch_function_start name start end
    // Each time we create a function, define a macro that adjusts the current
    // text location to position at a specific exception handler.
    //
    // We need to reset the macro each function, because the offset needs
    // to be at a fixed location after the function's start.
    .macro exception_vector base offset
        .skip (\base + \offset) - (. - \name)
    .endm
.endm
.macro arch_function_end name start end
    .purgem exception_vector
.endm

// Signals test completion by writing 0 to EXIT_TEST_ADDR.
.macro test_complete
    mov x0, EXIT_TEST_ADDR
    str xzr, [x0]
    b   .
.endm

// Signals test failure by writing 0 to EXIT_TEST_FAILURE_ADDR.
.macro test_failure
    movlit x0, EXIT_TEST_FAILURE_ADDR
    str    xzr, [x0]
    b      .
.endm

// Signals test completion by writing 0 to EXIT_TEST_ADDR.
.macro test_complete_aarch32
    .word 0xE3A01000    // aarch32:     movw r1, 0
    .word 0xE30F0000    // aarch32:     movw r0, [lower]EXIT_TEST_ADDR
    .word 0xE34000FF    // aarch32:     movw r0, [upper]EXIT_TEST_ADDR
    .word 0xE5801000    // aarch32:     str  r0, [r1]
    .word 0xEAFFFFFE    // aarch32: 1:  b    1b
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

    adr x0, .Ldrop_to_el0_end\@          // resume execution at label ahead
    msr elr_el1, x0                      // store exception link register
    eret                                 // resume at el0 using specified arch.
.Ldrop_to_el0_end\@:
.endm

// Set up and enable the MMU.
//
// Clobbers x0 and x1.
.macro setup_and_enable_mmu
    // Set up Memory Attribute Indirection Register (MAIR).
    movlit x0, GUEST_MAIR_EL1
    msr    mair_el1, x0

    // Set up Translation Control Register (TCR).
    movlit x0, GUEST_TCR_EL1
    msr    tcr_el1, x0

    // Set up page table root
    movlit x0, PAGE_TABLE_PADDR
    msr    ttbr0_el1, x0

    // Enable page table and caches.
    mrs     x0, sctlr_el1
    movlit  x1, (SCTLR_ELX_M | SCTLR_ELX_C | SCTLR_ELX_I)
    orr     x0, x0, x1
    msr     sctlr_el1, x0
    dsb sy
    isb
.endm

// Turn off the MMU.
//
// Clobbers x0.
.macro disable_mmu
    mrs     x0, sctlr_el1
    bic     x0, x0, SCTLR_ELX_M   // Clear MMU enable bit.
    msr     sctlr_el1, x0
    dsb sy
    isb
.endm

// Write the value in `word_reg` to every word in [start, start + size).
//
// Clobbers x0, x1.
.macro write_region start, size, word_reg
    movlit  x0, \start            // Start address
    movlit  x1, (\start + \size)  // End address
.Lwrite_region\@:
    str     \word_reg, [x0], 8    // Write the word and increment the pointer.
    cmp     x0, x1                // Keep looping until we reach the end address.
    bne     .Lwrite_region\@
.endm

// Confirm the region of memory [start, start + size) contains the value in `word_reg`.
//
// On error, jumps to the label `failure_label`. Otherwise, continues execution
// below the macro.
//
// Clobbers x0, x1, x2.
.macro check_region start, size, word_reg, failure_label
    movlit  x0, \start            // Start address
    movlit  x1, (\start + \size)  // End address
.Lcheck_region\@:
    ldr     x2, [x0], 8           // Read the word and increment the pointer.
    cmp     x2, \word_reg         // Ensure if matches
    bne     \failure_label
    cmp     x0, x1                // Otherwise, keep looping until we reach the end address.
    bne     .Lcheck_region\@
.endm


.text

// Test vcpu_enter.
FUNCTION vcpu_enter
    test_complete
END_FUNCTION

FUNCTION vcpu_wait
1:
    wfi
    b 1b
END_FUNCTION

// Test vcpu_read_state and vcpu_write_state.
FUNCTION vcpu_read_write_state
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
END_FUNCTION

// Test vcpu_interrupt.
FUNCTION vcpu_interrupt
    msr daifclr, #2
    b .

    exception_vector CURRENT_EL_ELx_BASE, IRQ_EXC_OFFSET
    test_complete
END_FUNCTION

// Test guest_set_trap using a memory-based trap.
FUNCTION guest_set_trap
    mov x0, TRAP_ADDR
    str xzr, [x0]
    test_complete
END_FUNCTION

// Test wfi instruction handling.
FUNCTION vcpu_wfi
    // Setup the virtual timer by:
    // 1. Setting the compare value to 0.
    // 2. Enabling the virtual timer.
    msr cntv_cval_el0, xzr
    mov x0, 1
    msr cntv_ctl_el0, x0

    wfi
    test_complete
END_FUNCTION

// Test wfi handling with pending interrupt in LR (GICv2).
FUNCTION vcpu_wfi_pending_interrupt_gicv2
    msr daifclr, #2
    b .

    exception_vector CURRENT_EL_ELx_BASE, IRQ_EXC_OFFSET
    movlit x0, GICC_BASE
    // Acknowledge interrupt by reading IAR.
    ldr w1, [x0, GICC_IAR_OFFSET]
    wfi
    test_complete
END_FUNCTION

// Test wfi handling with pending interrupt in LR (GICv3).
FUNCTION vcpu_wfi_pending_interrupt_gicv3
    msr daifclr, #2
    b .

    exception_vector CURRENT_EL_ELx_BASE, IRQ_EXC_OFFSET
    movlit x0, GICC_BASE
    // Acknowledge interrupt by reading IAR.
    mrs x1, ICC_IAR1_EL1
    dsb sy
    wfi
    test_complete
END_FUNCTION

// Test wfi instruction handling.
// Execution of WFI at EL0 on AARCH32 is propagated to EL1 / AARCH64.
FUNCTION vcpu_wfi_aarch32
    drop_to_el0 aarch32
    .word 0xE320F003         // aarch32: wfi

    // From ARM DDI 0487C.a, Section D10.2.45: Since a WFI can complete at any
    // time, even without a Wakeup event, the traps on WFI are not guaranteed to
    // be taken, even if the WFI is executed when there is no Wakeup event. The
    // only guarantee is that if the instruction does not complete in finite
    // time in the absence of a Wakeup event, the trap will be taken.
    //
    // We therefore complete the test at this point.
    test_complete_aarch32

    exception_vector LOWER_EL_ARCH32_BASE, SYNC_EXC_OFFSET
    test_complete
END_FUNCTION

// Test floating-point instruction handling with trapping between levels.
FUNCTION vcpu_fp
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
END_FUNCTION

// Test wfi instruction handling.
FUNCTION vcpu_fp_aarch32
    drop_to_el0 aarch32
    // Load double precision register d0 from address 0.
    // This should trigger floating point exception.
    .word 0xE3A00000         // aarch32: mov r0, 0
    .word 0xED900B00         // aarch32: vldr d0, [r0]
    test_complete_aarch32

    exception_vector LOWER_EL_ARCH32_BASE, SYNC_EXC_OFFSET
    mov x0, CPACR_EL1_FPEN_NO_TRAP
    msr cpacr_el1, x0
    eret
END_FUNCTION

FUNCTION vcpu_psci_system_off
    movlit x0, PSCI64_SYSTEM_OFF
    smc #0
END_FUNCTION

FUNCTION vcpu_psci_system_reset
    movlit x0, PSCI64_SYSTEM_RESET
    smc #0
END_FUNCTION

// Test set/way d-cache operations.
//
// These are trapped and emulated by the hypervisor.
FUNCTION vcpu_dc_set_way_ops
    mov x0, #0
    dc  csw, x0    // Clean L1 cache, set/way 0.
    dc  cisw, x0   // Clean/Invalidate L1 cache, set/way 0.
    dc  isw, x0    // Invalidate L1 cache, set/way 0.
    test_complete
END_FUNCTION

// Enable the MMU.
FUNCTION vcpu_enable_mmu
    // Write a value to raw memory.
    movlit  x14, 0x0123456789abcdef
    write_region GUEST_SCRATCH_ADDR, MEMORY_TEST_BYTES, x14  // clobbers x0, x1

    // Turn on the MMU.
    setup_and_enable_mmu  // clobbers x0, x1

    // Ensure we can read from memory, and we get our previously written value.
    //
    // Clobbers x0 -- x2, L1
    check_region GUEST_SCRATCH_ADDR, MEMORY_TEST_BYTES, x14, 1f

    test_complete

1:
    test_failure
END_FUNCTION

// Enable and disable the MMU.
FUNCTION vcpu_enable_disable_mmu
    // Turn on the MMU.
    setup_and_enable_mmu  // clobbers x0, x1

    // Write a magic word to every word on a page of cached memory.
    movlit  x14, 0x1122334455667788
    write_region GUEST_SCRATCH_ADDR, MEMORY_TEST_BYTES, x14  // clobbers x0, x1

    // Turn off the MMU.
    disable_mmu  // clobbers x0

    // Clean/invalidate caches.
    data_cache_way_set_op csw  // clobbers x0 -- x13.

    // The value written into cache should now have been cleaned to main
    // memory, and should be visible even though caches are off.
    check_region GUEST_SCRATCH_ADDR, MEMORY_TEST_BYTES, x14, .Lfailure  // clobbers x0 -- x2

    // Write out a different value to uncached RAM.
    movlit  x14, 0x8877665544332211
    write_region GUEST_SCRATCH_ADDR, MEMORY_TEST_BYTES, x14  // clobbers x0, x1, L1

    // Turn the MMU on again.
    setup_and_enable_mmu

    // Verify that what we wrote is visible, even with caches on.
    //
    // We've already invalidated caches and we are supposed to be the
    // only thing running on this system, so we shouldn't need to
    // invalidate any cache lines.
    check_region GUEST_SCRATCH_ADDR, MEMORY_TEST_BYTES, x14, .Lfailure  // clobbers x0 -- x2

    test_complete

    // Failure
.Lfailure:
    test_failure
END_FUNCTION

// A guest that just exits each time it is resumed.
FUNCTION vcpu_always_exit
    1:
    mov x0, TRAP_ADDR
    str xzr, [x0]
    b  1b
END_FUNCTION

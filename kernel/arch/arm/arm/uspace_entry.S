// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>

.syntax unified
.text

// void arm_uspace_entry(uintptr_t arg1, uintptr_t arg2, uint32_t spsr, vaddr_t kstack, uintptr_t sp, uint32_t zero, uintptr_t pc) __NO_RETURN;
// r0 = arg1
// r1 = arg2
// r2 = spsr
// r3 = kstack
// [sp] = sp
// [sp, #4] = 0
// [sp, #8] = pc
FUNCTION(arm_uspace_entry)
    // Get the user PC into lr, where it has to be for the magic "movs pc, lr".
    ldr lr, [sp, #8]

    // Load user-mode sp (5th argument) and lr (6th argument, always zero).
    ldm sp, {sp, lr}^

    // Install the SPSR; it will be restored into the CPSR by the magic
    // "movs pc, lr" instruction.
    msr spsr, r2

    // Switch to the kernel stack.
    mov sp, r3

    // Clear other registers.  r0 and r1 are already the userland arguments.
    mov r2, #0
    mov r3, #0
    mov r4, #0
    mov r5, #0
    mov r6, #0
    mov r7, #0
    mov r8, #0
    mov r9, #0
    mov r10, #0
    mov r11, #0
    mov r12, #0
    // Don't zero r13 and r14, they're per EL (SP and LR)
    // Don't zero r15, it's PC

    // Clear TLS registers.
    mcr p15, 0, r2, c13, c0, 2
    mcr p15, 0, r2, c13, c0, 3

    // The user thread arguments are in r0 and r1.  Jump to user mode now.
    movs pc, lr

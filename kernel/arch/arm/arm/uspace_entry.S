// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>

// void arm_uspace_entry(void *thread_arg, vaddr_t kstack, vaddr_t *ustack, uint32_t spsr, vaddr_t entry_point) __NO_RETURN;
FUNCTION(arm_uspace_entry)
    // Move entry_point into LR
    pop { r14 }

    // Set usermode SP
    ldmia r2, { sp }^
    // Set usermode LR
    mov r2, #0
    push { r2 }
    ldmia sp, { lr }^
    pop { r2 }

    msr spsr, r3
    mov sp, r1

    mov r1, #0
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

    mcr p15, 0, r1, c13, c0, 2
    mcr p15, 0, r1, c13, c0, 3

    // thread_arg is already in r0, jump to usermode now
    movs pc, r14

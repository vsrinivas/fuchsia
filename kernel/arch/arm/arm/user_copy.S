// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <asm.h>
#include <err.h>

// We do not need to check whether the addresses are in the
// kernel or virtual address spaces, since we only access them
// using user privileges.

.syntax unified
.arm

// status_t _arm_copy_from_user(void *dst, const void *src, size_t len, void **fault_return)
FUNCTION(_arm_copy_from_user)
    // Setup data fault return
    adr ip, .Lfault_from_user
    str ip, [r3]

    // Perform the memcpy
    cmp r2, #0
    beq 0f
.Lcopy_byte_from_user:
    ldrbt ip, [r1], #1
    strb ip, [r0], #1
    subs r2, #1
    bne .Lcopy_byte_from_user
0:

    mov r0, #NO_ERROR
    b .Lcleanup_from_user
.Lfault_from_user:
    mov r0, #ERR_INVALID_ARGS
.Lcleanup_from_user:
    // Reset data fault return
    mov ip, #0
    str ip, [r3]
    bx lr

// status_t _arm_copy_to_user(void *dst, const void *src, size_t len, void **fault_return)
FUNCTION(_arm_copy_to_user)
    // Setup data fault return
    adr ip, .Lfault_to_user
    str ip, [r3]

    // Perform the memcpy
    cmp r2, #0
    beq 0f
.Lcopy_byte_to_user:
    ldrb ip, [r1], #1
    strbt ip, [r0], #1
    subs r2, #1
    bne .Lcopy_byte_to_user
0:

    mov r0, #NO_ERROR
    b .Lcleanup_to_user
.Lfault_to_user:
    mov r0, #ERR_INVALID_ARGS
.Lcleanup_to_user:
    // Reset data fault return
    mov ip, #0
    str ip, [r3]
    bx lr

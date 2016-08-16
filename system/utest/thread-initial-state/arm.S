// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "asm.h"

.macro check_general_purpose reg
    cmp \reg, #0
    bne .Lfail
.endm

// int thread_entry(void *arg)
FUNCTION(thread_entry)
    bmi .Lfail
    beq .Lfail
    bcs .Lfail
    bvs .Lfail

    check_general_purpose r1
    check_general_purpose r2
    check_general_purpose r3
    check_general_purpose r4
    check_general_purpose r5
    check_general_purpose r6
    check_general_purpose r7
    check_general_purpose r8
    check_general_purpose r9
    check_general_purpose r10
    check_general_purpose r11
    check_general_purpose r12
    // Don't check r13, it's SP
    check_general_purpose r14
    // Don't check r15, it's PC

    // test thread arg
    ldr r1, =0x90abcdef
    sub r0, r0, r1
    cmp r0, #0
    bne .Lfail

    // TODO: Test floating point/simd if we support
    b mx_thread_exit
.Lfail:
    b print_fail

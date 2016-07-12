// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

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

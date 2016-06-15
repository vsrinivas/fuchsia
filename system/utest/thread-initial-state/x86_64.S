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
    cmpq $0, \reg
    jne .Lfail
.endm

.macro check_xmm reg
    ptest \reg, \reg
    jnz .Lfail
.endm

.macro check_mm reg
    movq \reg, %rax
    cmpq $0, %rax
    jne .Lfail
.endm

.macro check_segment reg
    mov \reg, %ax
    cmpw $0, %ax
    jne .Lfail
.endm

# int thread_entry(void *arg)
FUNCTION(thread_entry)
    jz .Lfail
    jc .Lfail
    js .Lfail
    jp .Lfail

    check_general_purpose %rax
    check_general_purpose %rbx
    check_general_purpose %rcx
    check_general_purpose %rdx
    check_general_purpose %rbp
    check_general_purpose %rsi
    check_general_purpose %r8
    check_general_purpose %r9
    check_general_purpose %r10
    check_general_purpose %r11
    check_general_purpose %r12
    check_general_purpose %r13
    check_general_purpose %r14
    check_general_purpose %r15

    # test thread arg
    movq $0x1234567890abcdef, %rax
    cmpq %rax, %rdi
    jne .Lfail

    # Don't check cs/ss since those are set by the kernel explicitly
    check_segment %ds
    check_segment %es
    check_segment %fs
    check_segment %gs

    check_xmm %xmm0
    check_xmm %xmm1
    check_xmm %xmm2
    check_xmm %xmm3
    check_xmm %xmm4
    check_xmm %xmm5
    check_xmm %xmm6
    check_xmm %xmm7

    check_mm %mm0
    check_mm %mm1
    check_mm %mm2
    check_mm %mm3
    check_mm %mm4
    check_mm %mm5
    check_mm %mm6
    check_mm %mm7

    # TODO: Test ymm registers once they exist
    jmp _magenta_thread_exit
.Lfail:
    jmp print_fail

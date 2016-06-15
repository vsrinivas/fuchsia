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

/* define and implement the magenta syscall wrappers for x86-64 */

.text

#define MAGENTA_SYSCALL_MAGIC 0x00ff00ff00000000

.macro _syscall nargs, name, n
.globl \name
.type \name,STT_FUNC
\name:
.if \nargs <= 6
    push   %r10
    push   %r11
    mov    %rcx, %r10
    movabs $(MAGENTA_SYSCALL_MAGIC | \n), %rax
    syscall
    pop    %r11
    pop    %r10
    ret
.endif
.if \nargs == 7
    push   %r10
    push   %r11
    push   %r12
    mov    0x20(%rsp), %r12
    mov    %rcx, %r10
    movabs $(MAGENTA_SYSCALL_MAGIC | \n), %rax
    syscall
    pop    %r13
    pop    %r12
    pop    %r11
    ret
.endif
.if \nargs == 8
    push   %r10
    push   %r11
    push   %r12
    push   %r13
    mov    0x28(%rsp), %r12
    mov    0x30(%rsp), %r13
    mov    %rcx, %r10
    movabs $(MAGENTA_SYSCALL_MAGIC | \n), %rax
    syscall
    pop    %r13
    pop    %r12
    pop    %r11
    pop    %r10
    ret
.endif
.size \name, . - \name
.endm

#define MAGENTA_SYSCALL_DEF(nargs64, nargs32, n, ret, name, args...) _syscall nargs64, _magenta_##name, n

#include <magenta/syscalls.inc>

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

/* define and implement the magenta syscall wrappers for 32bit arm */

.text

#define MAGENTA_SYSCALL_MAGIC 0xf0f0

.macro syscall nargs, name, n
.globl \name
.type \name,STT_FUNC
\name:
.if \nargs < 5
    movw   r12, #\n
    movt   r12, #MAGENTA_SYSCALL_MAGIC
    svc    #0xff00ff
    mov    r12, #0
    bx     lr
.endif
.if \nargs == 5
    push   {r4-r5}
    ldr    r4, [sp, #8]
    movw   r12, #\n
    movt   r12, #MAGENTA_SYSCALL_MAGIC
    svc    #0xff00ff
    mov    r12, #0
    pop    {r4-r5}
    bx     lr
.endif
.if \nargs == 6
    push   {r4-r5}
    ldrd   r4, r5, [sp, #8]
    movw   r12, #\n
    movt   r12, #MAGENTA_SYSCALL_MAGIC
    svc    #0xff00ff
    mov    r12, #0
    pop    {r4-r5}
    bx     lr
.endif
.if \nargs == 7
    push   {r4-r7}
    ldrd   r4, r5, [sp, #16]
    ldr    r6, [sp, #24]
    movw   r12, #\n
    movt   r12, #MAGENTA_SYSCALL_MAGIC
    svc    #0xff00ff
    mov    r12, #0
    pop    {r4-r7}
    bx     lr
.endif
.if \nargs == 8
    push   {r4-r7}
    ldrd   r4, r5, [sp, #16]
    ldrd   r6, r7, [sp, #24]
    movw   r12, #\n
    movt   r12, #MAGENTA_SYSCALL_MAGIC
    svc    #0xff00ff
    mov    r12, #0
    pop    {r4-r7}
    bx     lr
.endif
.size \name, . - \name
.endm

#if LIBDDK
#define MAGENTA_SYSCALL_DEF(a...)
#define MAGENTA_DDKCALL_DEF(nargs64, nargs32, n, ret, name, args...) syscall nargs32, mx_##name, n
#else
#define MAGENTA_DDKCALL_DEF(a...)
#define MAGENTA_SYSCALL_DEF(nargs64, nargs32, n, ret, name, args...) syscall nargs32, mx_##name, n
#endif

#include <magenta/syscalls.inc>



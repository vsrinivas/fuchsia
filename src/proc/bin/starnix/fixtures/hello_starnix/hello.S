// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

.section .data
    msg:
        .ascii "hello starnix\n"
.section .text
.globl _start
_start:
    movq  $1, %rax
    movq  $1, %rdi
    lea  msg(%rip), %rsi
    movq  $14, %rdx
    syscall

    movq  $60, %rax
    movq  $0, %rdi
    syscall
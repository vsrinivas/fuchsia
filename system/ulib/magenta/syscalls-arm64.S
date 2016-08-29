// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/* define and implement the magenta syscall wrappers for arm64 */

.text

.cfi_sections .eh_frame, .debug_frame

.macro syscall name, n
.globl \name
.type \name,STT_FUNC
\name:
    .cfi_startproc
    mov    x16, #\n
    svc    #0xf0f /* magic value to differentiate magenta syscalls */
    ret
    .cfi_endproc
.size \name, . - \name
.endm

#define MAGENTA_SYSCALL_DEF(nargs64, nargs32, n, ret, name, args...) syscall mx_##name, n

#include <magenta/syscalls.inc>

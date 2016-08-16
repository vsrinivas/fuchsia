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

#if LIBDDK
#define MAGENTA_SYSCALL_DEF(a...)
#define MAGENTA_DDKCALL_DEF(nargs64, nargs32, n, ret, name, args...) syscall mx_##name, n
#else
#define MAGENTA_DDKCALL_DEF(a...)
#define MAGENTA_SYSCALL_DEF(nargs64, nargs32, n, ret, name, args...) syscall mx_##name, n
#endif

#include <magenta/syscalls.inc>

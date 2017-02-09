// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/* define and implement the magenta syscall wrappers for arm64 */

.text

.cfi_sections .eh_frame, .debug_frame

.macro m_syscall name, n
.globl _\name
.type _\name,STT_FUNC
_\name:
    .cfi_startproc
    mov    x16, #\n
    svc    #0xf0f /* magic value to differentiate magenta syscalls */
    ret
    .cfi_endproc
.size _\name, . - _\name
.weak \name
.type \name,STT_FUNC
\name = _\name
.size \name, . - _\name
.endm

#include "gen-arm64.S"

#include "syscalls-stubs.S"

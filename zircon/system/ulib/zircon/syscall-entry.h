// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

.macro syscall_entry_begin name
.globl SYSCALL_\name
.hidden SYSCALL_\name
.type SYSCALL_\name,STT_FUNC
SYSCALL_\name:
.cfi_startproc
.endm

.macro syscall_entry_end name public=1
.cfi_endproc
.size SYSCALL_\name, . - SYSCALL_\name

// Create a hidden alias for the syscall which is prefixed with CODE_.  This
// allows the macros which perform redirection in the kernel to redirect a VDSO
// entry to either an explicit CODE_ alternate, or to another syscall if needed.
.globl CODE_SYSCALL_\name
.hidden CODE_SYSCALL_\name
CODE_SYSCALL_\name = SYSCALL_\name
.size CODE_SYSCALL_\name, . - SYSCALL_\name

// For wrapper functions, aliasing is handled by the generator.
.if \public
.globl _\name
.type _\name,STT_FUNC
_\name = SYSCALL_\name
.size _\name, . - SYSCALL_\name

.weak \name
.type \name,STT_FUNC
\name = SYSCALL_\name
.size \name, . - SYSCALL_\name

.globl VDSO_\name
.hidden VDSO_\name
.type VDSO_\name,STT_FUNC
VDSO_\name = SYSCALL_\name
.size VDSO_\name, . - SYSCALL_\name
.endif

.endm

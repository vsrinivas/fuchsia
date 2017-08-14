// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/* define and implement the magenta syscall wrappers for x86-64 */

#include "syscall-entry.h"
#include "magenta-syscall-x86-64.S"

.text

.cfi_sections .eh_frame, .debug_frame

// The following assembly code converts arguments from the x86-64 SysV
// ABI's function calling conventions to the conventions used for Magenta
// syscalls:
//
//   arg 1: stays in %rdi
//   arg 2: stays in %rsi
//   arg 3: stays in %rdx
//   arg 4: moved from %rcx to %r10
//   arg 5: stays in %r8
//   arg 6: stays in %r9
//   arg 7: moved from 8(%rsp) to %r12
//   arg 8: moved from 16(%rsp) to %r13

.macro m_syscall name, num, nargs, public
syscall_entry_begin \name
    .cfi_same_value %r12
    .cfi_same_value %r13
.if \nargs <= 3
    magenta_syscall \num, \name, \name
    ret
.elseif \nargs <= 6
    mov      %rcx, %r10  // Argument 4
    magenta_syscall \num, \name, \name
    ret
.elseif \nargs == 7
    push_reg %r12
    mov      0x10(%rsp), %r12  // Argument 7
    mov      %rcx, %r10  // Argument 4
    magenta_syscall \num, \name, \name
    pop_reg  %r12
    ret
.elseif \nargs == 8
    push_reg %r12
    push_reg %r13
    mov      0x18(%rsp), %r12  // Argument 7
    mov      0x20(%rsp), %r13  // Argument 8
    mov      %rcx, %r10  // Argument 4
    magenta_syscall \num, \name, \name
    pop_reg  %r13
    pop_reg  %r12
    ret
.endif
syscall_entry_end \name \public
.endm

#include <magenta/syscalls-x86-64.S>

#include "syscalls-stubs.S"

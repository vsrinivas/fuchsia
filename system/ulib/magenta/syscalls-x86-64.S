// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/* define and implement the magenta syscall wrappers for x86-64 */

#include "syscall-entry.h"
#include "magenta-syscall-x86-64.S"

.text

.cfi_sections .eh_frame, .debug_frame

.macro m_syscall nargs, name, num, with_aliases
syscall_entry_begin \name
    .cfi_same_value %r10
    .cfi_same_value %r11
    .cfi_same_value %r12
    .cfi_same_value %r13
.if \nargs <= 6
    push_reg %r10
    push_reg %r11
    mov      %rcx, %r10
    magenta_syscall \num, \name, \name
    pop_reg  %r11
    pop_reg  %r10
    ret
.endif
.if \nargs == 7
    push_reg %r10
    push_reg %r11
    push_reg %r12
    mov      0x20(%rsp), %r12
    mov      %rcx, %r10
    magenta_syscall \num, \name, \name
    pop_reg  %r12
    pop_reg  %r11
    pop_reg  %r10
    ret
.endif
.if \nargs == 8
    push_reg %r10
    push_reg %r11
    push_reg %r12
    push_reg %r13
    mov      0x28(%rsp), %r12
    mov      0x30(%rsp), %r13
    mov      %rcx, %r10
    magenta_syscall \num, \name, \name
    pop_reg  %r13
    pop_reg  %r12
    pop_reg  %r11
    pop_reg  %r10
    ret
.endif
syscall_entry_end \name \with_aliases
.endm

#include <magenta/syscalls-x86-64.S>

#include "syscalls-stubs.S"

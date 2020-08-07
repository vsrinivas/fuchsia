// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/* define and implement the zircon syscall wrappers for riscv64 */

#include "syscall-entry.h"
#include "zircon-syscall-riscv64.S"

.text

.cfi_sections .eh_frame, .debug_frame

.macro m_syscall name, num, nargs, public
syscall_entry_begin \name
    zircon_syscall \num, \name, \name
    ret
syscall_entry_end \name \public
.endm

#include "syscalls-stubs.S"

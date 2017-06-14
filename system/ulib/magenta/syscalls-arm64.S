// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/* define and implement the magenta syscall wrappers for arm64 */

#include "syscall-entry.h"
#include "magenta-syscall-arm64.S"

.text

.cfi_sections .eh_frame, .debug_frame

.macro m_syscall name, num, nargs, public
syscall_entry_begin \name
    magenta_syscall \num, \name, \name
    ret
syscall_entry_end \name \public
.endm

#include <magenta/syscalls-arm64.S>

#include "syscalls-stubs.S"

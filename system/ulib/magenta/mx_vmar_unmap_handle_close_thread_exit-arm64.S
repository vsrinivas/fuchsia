// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "syscall-entry.h"
#include "magenta-syscall-arm64.S"
#include <magenta/mx-syscall-numbers.h>

.text

.cfi_sections .eh_frame, .debug_frame

// (vmar_handle: x0, addr: x1, len: x2, handle: x3)
syscall_entry_begin mx_vmar_unmap_handle_close_thread_exit

    // Save the handle argument in a callee-saves register (x19).
    // Callee-save that register so we can unwind in the error case.
    // We only need to save x19, but we have to keep SP aligned to 16
    // bytes, so we always push and pop registers in pairs.
    push_regpair x19, x20
    mov x19, x3

    magenta_syscall MX_SYS_vmar_unmap, mx_vmar_unmap, mx_vmar_unmap_handle_close_thread_exit
    cbnz x0, .Lvmar_unmap_fail

    // Now the stack is gone and we can never return!

    mov x0, x19
    magenta_syscall MX_SYS_handle_close, mx_handle_close, mx_vmar_unmap_handle_close_thread_exit
    cbnz x0, .Lhandle_close_fail

    magenta_syscall MX_SYS_thread_exit, mx_thread_exit, mx_vmar_unmap_handle_close_thread_exit

    // It should be impossible to get here.
.Lthread_exit_returned:
    brk #1000

.Lvmar_unmap_fail:
    pop_regpair x19, x20
    ret

.Lhandle_close_fail:
    brk #1000

syscall_entry_end mx_vmar_unmap_handle_close_thread_exit

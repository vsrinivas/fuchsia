// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "syscall-entry.h"
#include "magenta-syscall-arm64.S"
#include <magenta/mx-syscall-numbers.h>

.text

.cfi_sections .eh_frame, .debug_frame

// (value_ptr: x0, count: w1, handle: w2)
syscall_entry_begin mx_futex_wake_handle_close_thread_exit

    // Save the handle argument in a callee-saves register (x19).
    // Callee-save that register so we can unwind in the error case.
    // We only need to save x19, but we have to keep SP aligned to 16
    // bytes, so we always push and pop registers in pairs.
    push_regpair x19, x20
    mov w19, w2

    magenta_syscall MX_SYS_futex_wake, mx_futex_wake, mx_futex_wake_handle_close_thread_exit
    cbnz x0, .Lfutex_wake_fail

    // Now the stack might be gone and we can never return!

    mov w0, w19
    magenta_syscall MX_SYS_handle_close, mx_handle_close, mx_futex_wake_handle_close_thread_exit
    cbnz x0, .Lhandle_close_fail

    magenta_syscall MX_SYS_thread_exit, mx_thread_exit, mx_futex_wake_handle_close_thread_exit

    // It should be impossible to get here.
.Lthread_exit_returned:
    brk #1000

.Lfutex_wake_fail:
    pop_regpair x19, x20
    ret

.Lhandle_close_fail:
    brk #1000

syscall_entry_end mx_futex_wake_handle_close_thread_exit

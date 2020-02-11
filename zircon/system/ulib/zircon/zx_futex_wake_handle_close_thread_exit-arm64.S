// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "syscall-entry.h"
#include "zircon-syscall-arm64.S"
#include <lib/syscalls/zx-syscall-numbers.h>

.text

.cfi_sections .eh_frame, .debug_frame

// (value_ptr: x0, wake_count: w1, new_value: w2, handle: w3)
syscall_entry_begin zx_futex_wake_handle_close_thread_exit

    // Store the value into the futex, which should signal that the stack is no longer in use.
    // atomic_store_explicit(value_ptr, new_value, memory_order_release)
    stlr w2, [x0]

    // Now the stack might be gone and we can never return!

    // Save the handle argument in a callee-saves register (x19).
    // Since this function cannot return, do not bother preserving the old contents.
    mov w19, w3

    zircon_syscall ZX_SYS_futex_wake, zx_futex_wake, zx_futex_wake_handle_close_thread_exit
    cbnz x0, .Lfutex_wake_fail

    mov w0, w19
    zircon_syscall ZX_SYS_handle_close, zx_handle_close, zx_futex_wake_handle_close_thread_exit
    cbnz x0, .Lhandle_close_fail

    zircon_syscall ZX_SYS_thread_exit, zx_thread_exit, zx_futex_wake_handle_close_thread_exit

    // It should be impossible to get here.
.Lthread_exit_returned:
    brk #1000

    // We can't recover in these cases, since our stack may have been freed.
.Lfutex_wake_fail:
    brk #1000
.Lhandle_close_fail:
    brk #1000

syscall_entry_end zx_futex_wake_handle_close_thread_exit

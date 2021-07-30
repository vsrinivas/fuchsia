// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "syscall-entry.h"
#include "zircon-syscall-riscv64.S"
#include <lib/syscalls/zx-syscall-numbers.h>

.text

.cfi_sections .eh_frame, .debug_frame

// (value_ptr: a0, wake_count: a1, new_value: a2, handle: a3)
syscall_entry_begin zx_futex_wake_handle_close_thread_exit

    // Store the value into the futex, which should signal that the stack is no longer in use.
    // TODO(revest): atomic_store_explicit(value_ptr, new_value, memory_order_release)
    sd a2, (a0)

    // Now the stack might be gone and we can never return!

    // Save the handle argument in a callee-saves register (s0).
    // Since this function cannot return, do not bother preserving the old contents.
    mv s0, a3

    zircon_syscall ZX_SYS_futex_wake, zx_futex_wake, zx_futex_wake_handle_close_thread_exit
    bnez a0, .Lfutex_wake_fail

    mv a0, s0
    zircon_syscall ZX_SYS_handle_close, zx_handle_close, zx_futex_wake_handle_close_thread_exit
    bnez a0, .Lhandle_close_fail

    zircon_syscall ZX_SYS_thread_exit, zx_thread_exit, zx_futex_wake_handle_close_thread_exit

    // It should be impossible to get here.
.Lthread_exit_returned:
    ebreak

    // We can't recover in these cases, since our stack may have been freed.
.Lfutex_wake_fail:
    ebreak
.Lhandle_close_fail:
    ebreak

syscall_entry_end zx_futex_wake_handle_close_thread_exit

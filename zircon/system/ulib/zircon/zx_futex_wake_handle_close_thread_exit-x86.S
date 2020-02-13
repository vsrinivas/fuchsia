// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "syscall-entry.h"
#include "zircon-syscall-x86.S"
#include <zircon/zx-syscall-numbers.h>

.text

.cfi_sections .eh_frame, .debug_frame

// (value_ptr: %rdi, wake_count: %esi, new_value: %edx, handle: %ecx)
syscall_entry_begin zx_futex_wake_handle_close_thread_exit

    // Store the value into the futex, which should signal that the stack is no longer in use.
    // atomic_store_explicit(value_ptr, new_value, memory_order_release)
    mov %edx, (%rdi)

    // Now the stack might be gone and we can never return!

    // Save the handle argument in a callee-saves register (%rbx).
    // Since this function cannot return, do not bother preserving the old contents.
    mov %ecx, %ebx

    zircon_syscall ZX_SYS_futex_wake, zx_futex_wake, zx_futex_wake_handle_close_thread_exit
    test %eax, %eax
    jnz .Lfutex_wake_fail

    mov %ebx, %edi
    zircon_syscall ZX_SYS_handle_close, zx_handle_close, zx_futex_wake_handle_close_thread_exit
    test %eax, %eax
    jnz .Lhandle_close_fail

    zircon_syscall ZX_SYS_thread_exit, zx_thread_exit, zx_futex_wake_handle_close_thread_exit

    // It should be impossible to get here.
.Lthread_exit_returned:
    ud2

    // We can't recover in these cases, since our stack may have been freed.
.Lfutex_wake_fail:
    ud2
.Lhandle_close_fail:
    ud2

syscall_entry_end zx_futex_wake_handle_close_thread_exit

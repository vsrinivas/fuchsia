// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "syscall-entry.h"
#include "magenta-syscall-x86-64.S"
#include <magenta/mx-syscall-numbers.h>

.text

.cfi_sections .eh_frame, .debug_frame

// (value_ptr: %rdi, count: %esi, handle: %edx)
syscall_entry_begin mx_futex_wake_handle_close_thread_exit

    // Save the handle argument in a callee-saves register (%rbx).
    // Callee-save that register so we can unwind in the error case.
    push_reg %rbx
    mov %edx, %ebx

    magenta_syscall MX_SYS_futex_wake, mx_futex_wake, mx_futex_wake_handle_close_thread_exit
    test %eax, %eax
    jnz .Lfutex_wake_fail

    // Now the stack might be gone and we can never return!

    mov %ebx, %edi
    magenta_syscall MX_SYS_handle_close, mx_handle_close, mx_futex_wake_handle_close_thread_exit
    test %eax, %eax
    jnz .Lhandle_close_fail

    magenta_syscall MX_SYS_thread_exit, mx_thread_exit, mx_futex_wake_handle_close_thread_exit

    // It should be impossible to get here.
.Lthread_exit_returned:
    ud2

.Lfutex_wake_fail:
    pop_reg %rbx
    ret

.Lhandle_close_fail:
    ud2

syscall_entry_end mx_futex_wake_handle_close_thread_exit

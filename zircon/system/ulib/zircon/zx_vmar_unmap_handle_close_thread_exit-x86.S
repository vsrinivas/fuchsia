// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "syscall-entry.h"
#include "zircon-syscall-x86.S"
#include <zircon/zx-syscall-numbers.h>

.text

.cfi_sections .eh_frame, .debug_frame

// (vmar_handle: %rdi, addr: %rsi, len: %rdx, handle: %ecx)
syscall_entry_begin zx_vmar_unmap_handle_close_thread_exit

    // Save the handle argument in a callee-saves register (%rbx).
    // Callee-save that register so we can unwind in the error case.
    push_reg %rbx
    mov %ecx, %ebx

    zircon_syscall ZX_SYS_vmar_unmap, zx_vmar_unmap, zx_vmar_unmap_handle_close_thread_exit
    test %eax, %eax
    jnz .Lvmar_unmap_fail

    // Now the stack is gone and we can never return!

    mov %ebx, %edi
    zircon_syscall ZX_SYS_handle_close, zx_handle_close, zx_vmar_unmap_handle_close_thread_exit
    test %eax, %eax
    jnz .Lhandle_close_fail

    zircon_syscall ZX_SYS_thread_exit, zx_thread_exit, zx_vmar_unmap_handle_close_thread_exit

    // It should be impossible to get here.
.Lthread_exit_returned:
    ud2

.Lvmar_unmap_fail:
    pop_reg %rbx
    ret

.Lhandle_close_fail:
    ud2

syscall_entry_end zx_vmar_unmap_handle_close_thread_exit

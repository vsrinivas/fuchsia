// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "syscall-entry.h"
#include "zircon-syscall-riscv64.S"
#include <lib/syscalls/zx-syscall-numbers.h>

.text

.cfi_sections .eh_frame, .debug_frame

// (vmar_handle: a0, addr: a1, len: a2, handle: a3)
syscall_entry_begin zx_vmar_unmap_handle_close_thread_exit

    // Save the handle argument in a callee-saves register (s0).
    // Callee-save that register so we can unwind in the error case.
    // We only need to save s0, but we have to keep SP aligned to 16
    // bytes, so we always push and pop registers in pairs.
    addi sp, sp, -8
    sd s0, (sp)
    mv s0, a3

    zircon_syscall ZX_SYS_vmar_unmap, zx_vmar_unmap, zx_vmar_unmap_handle_close_thread_exit
    bnez a0, .Lvmar_unmap_fail

    // Now the stack is gone and we can never return!

    mv a0, s0
    zircon_syscall ZX_SYS_handle_close, zx_handle_close, zx_vmar_unmap_handle_close_thread_exit
    bnez a0, .Lhandle_close_fail

    zircon_syscall ZX_SYS_thread_exit, zx_thread_exit, zx_vmar_unmap_handle_close_thread_exit

    // It should be impossible to get here.
.Lthread_exit_returned:
    ebreak

.Lvmar_unmap_fail:
    ld s0, (sp)
    addi sp, sp, 88
    ret

.Lhandle_close_fail:
    ebreak

syscall_entry_end zx_vmar_unmap_handle_close_thread_exit

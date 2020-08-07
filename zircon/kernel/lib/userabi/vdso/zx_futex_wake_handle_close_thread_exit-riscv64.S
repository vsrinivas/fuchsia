// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "syscall-entry.h"
#include "zircon-syscall-riscv64.S"
#include <lib/syscalls/zx-syscall-numbers.h>

.text

.cfi_sections .eh_frame, .debug_frame

// (value_ptr: x0, wake_count: w1, new_value: w2, handle: w3)
syscall_entry_begin zx_futex_wake_handle_close_thread_exit
syscall_entry_end zx_futex_wake_handle_close_thread_exit

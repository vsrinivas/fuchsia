// Copyright 2021 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_INCLUDE_KERNEL_RESTRICTED_H_
#define ZIRCON_KERNEL_INCLUDE_KERNEL_RESTRICTED_H_

#include <lib/user_copy/user_ptr.h>
#include <lib/zx/result.h>

// Routines and data structures to support restricted mode.

// Enter restricted mode on the current thread.
zx_status_t RestrictedEnter(uint32_t options, uintptr_t vector_table_ptr, uintptr_t context);

// Read/write the restricted state on the current thread.
// Currently do not support accessing state from another thread.
zx_status_t RestrictedReadState(user_out_ptr<void> data, size_t data_size);
zx_status_t RestrictedWriteState(user_in_ptr<const void> data, size_t data_size);

#endif  // ZIRCON_KERNEL_INCLUDE_KERNEL_RESTRICTED_H_

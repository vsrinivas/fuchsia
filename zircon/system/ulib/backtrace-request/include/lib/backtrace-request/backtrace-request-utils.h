// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef BACKTRACE_REQUEST_BACKTRACE_REQUEST_UTILS_H_
#define BACKTRACE_REQUEST_BACKTRACE_REQUEST_UTILS_H_

#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>
#include <zircon/types.h>

// Returns true if the given exception type and general registers indicate
// an exception caused by backtrace_request().
bool is_backtrace_request(zx_excp_type_t excp_type, const zx_thread_state_general_regs_t* regs);

// Cleans up the backtrace request so that resuming |thread| will allow it to
// continue execution normally.
//
// This must only be called if is_backtrace_request() returned true, and only
// once per backtrace exception.
//
// |regs| may be modified by this function.
zx_status_t cleanup_backtrace_request(zx_handle_t thread, zx_thread_state_general_regs_t* regs);

#endif // BACKTRACE_REQUEST_BACKTRACE_REQUEST_UTILS_H_

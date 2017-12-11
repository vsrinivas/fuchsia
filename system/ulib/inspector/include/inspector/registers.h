// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/types.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>

namespace inspector {

#if defined(__x86_64__)
using general_regs_type = zx_x86_64_general_regs_t;
using excp_data_type = x86_64_exc_data_t;
#elif defined(__aarch64__)
using general_regs_type = zx_arm64_general_regs_t;
using excp_data_type = arm64_exc_data_t;
#else   // unsupported arch
using general_regs_type = int;
using excp_data_type = int;
#endif

bool read_general_regs(zx_handle_t thread, general_regs_type* regs);

// If |excp_data| is non-NULL then print useful related exception data
// along with the registers.
void print_general_regs(const general_regs_type* regs,
                        const excp_data_type* excp_data);

}  // namespace inspector

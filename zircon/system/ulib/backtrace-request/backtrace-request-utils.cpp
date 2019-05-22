// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/backtrace-request/backtrace-request-utils.h>

#include <zircon/syscalls.h>

#include <lib/backtrace-request/backtrace-request.h>

namespace {

bool have_swbreak_magic(const zx_thread_state_general_regs_t* regs) {
#if defined(__x86_64__)
    return regs->rax == BACKTRACE_REQUEST_MAGIC;
#elif defined(__aarch64__)
    return regs->r[0] == BACKTRACE_REQUEST_MAGIC;
#else
    return false;
#endif
}

} // namespace

// TODO: consider disabling this feature for non-development builds.
bool is_backtrace_request(zx_excp_type_t excp_type, const zx_thread_state_general_regs_t* regs) {
    return excp_type == ZX_EXCP_SW_BREAKPOINT && regs != nullptr && have_swbreak_magic(regs);
}

zx_status_t cleanup_backtrace_request(zx_handle_t thread, zx_thread_state_general_regs_t* regs) {
#if defined(__x86_64__)
    // On x86, the pc is left at one past the s/w break insn,
    // so there's nothing more we need to do.
    return ZX_OK;
#elif defined(__aarch64__)
    // Skip past the brk instruction.
    regs->pc += 4;
    return zx_thread_write_state(thread, ZX_THREAD_STATE_GENERAL_REGS, regs, sizeof(*regs));
#else
    return ZX_ERR_NOT_SUPPORTED;
#endif
}

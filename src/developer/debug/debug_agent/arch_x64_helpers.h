// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_X64_HELPERS_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_X64_HELPERS_H_

#include <lib/zx/thread.h>
#include <zircon/syscalls/debug.h>
#include <zircon/syscalls/exception.h>

#include <vector>

#include "src/developer/debug/debug_agent/arch_helpers.h"
#include "src/developer/debug/debug_agent/arch_types.h"

namespace debug_ipc {
struct Register;
}  // namespace debug_ipc

namespace debug_agent {
namespace arch {

// Helper functions for defining x86 arch dependent behavior.
// They are on a separate header/implementation so that it can be more easily
// tested.

// Aligns the given address according to the watchpoint size.
uint64_t WatchpointAddressAlign(const debug_ipc::AddressRange&);

// Debug functions -------------------------------------------------------------

std::string GeneralRegistersToString(const zx_thread_state_general_regs&);

}  // namespace arch
}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_X64_HELPERS_H_

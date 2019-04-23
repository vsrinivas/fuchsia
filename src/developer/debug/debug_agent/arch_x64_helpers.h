// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include <lib/zx/thread.h>
#include <zircon/syscalls/exception.h>

#include "src/developer/debug/debug_agent/arch_x64.h"

namespace debug_ipc {
struct Register;
}  // namespace debug_ipc

namespace debug_agent {
namespace arch {

// Helper functions for defining x86 arch dependent behavior.
// They are on a separate header/implementation so that it can be more easily
// tested.

zx_status_t WriteGeneralRegisters(const std::vector<debug_ipc::Register>&,
                                  zx_thread_state_general_regs_t*);

// Returns the state the debug registers should be if we added a execution HW
// breakpoint for |address|.
// Returns ZX_ERR_ALREADY_BOUND if |address| is already installed.
// Returns ZX_ERR_NO_RESOURCES if there are no registers left.
zx_status_t SetupHWBreakpoint(uint64_t address, zx_thread_state_debug_regs_t*);

// Removes an installed execution HW breakpoint for |address|.
// If the address is not installed, no functional change will happen and
// ZX_ERR_OUT_OF_RANGE will be returned.
zx_status_t RemoveHWBreakpoint(uint64_t address, zx_thread_state_debug_regs_t*);

// Returns the state the debug registers should be if we added a watchpoing.
// for |address|.
// Returns ZX_ERR_ALREADY_BOUND if |address| is already installed.
// Returns ZX_ERR_NO_RESOURCES if there are no registers left.
zx_status_t SetupWatchpoint(uint64_t address, zx_thread_state_debug_regs_t*);

// Removes an installed execution watchpoint for |address|.
// If the address is not installed, no functional change will happen and
// ZX_ERR_OUT_OF_RANGE will be returned.
zx_status_t RemoveWatchpoint(uint64_t address, zx_thread_state_debug_regs_t*);

// Debug functions -------------------------------------------------------------

std::string GeneralRegistersToString(const zx_thread_state_general_regs&);

std::string DebugRegistersToString(const zx_thread_state_debug_regs_t&);

std::string DR6ToString(uint64_t dr6);

std::string DR7ToString(uint64_t dr7);

}  // namespace arch
}  // namespace debug_agent

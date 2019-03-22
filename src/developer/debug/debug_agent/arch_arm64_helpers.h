// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/syscalls/exception.h>

#include "src/developer/debug/debug_agent/arch_arm64.h"

namespace debug_agent {
namespace arch {

// Helpers for defining arm64 specific behavior.

// Fills the given state the debug registers to what it should be if we added
// an execution HW breakpoint for |address|.
// Return ZX_ERR_NO_RESOURCES if there are no registers left.
zx_status_t SetupHWBreakpoint(uint64_t address, zx_thread_state_debug_regs_t*);

// Removes an installed execution HW breakpoint for |address|.
// If the address is not installed, no functional change will happen and
// ZX_ERR_OUT_OF_RANGE will be returned.
zx_status_t RemoveHWBreakpoint(uint64_t address, zx_thread_state_debug_regs_t*);

// Useful function for debugging to keep around.
void PrintDebugRegisters(const zx_thread_state_debug_regs_t&);

}  // namespace arch
}  // namespace debug_agent

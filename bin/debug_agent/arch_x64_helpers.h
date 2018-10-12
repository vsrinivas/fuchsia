// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/syscalls/exception.h>

#include "garnet/bin/debug_agent/arch_x64.h"

namespace debug_agent {
namespace arch {

// Helper functions for defining x86 arch dependent behavior.
// They are on a separete header/implementation so that it can be more easily
// tested.

// Returns the state the debug registers should be if we added a execution HW
// breakpoint for |address|.
// return ZX_ERR_NO_RESOURCES if there are no registers left.
zx_status_t SetupDebugBreakpoint(uint64_t address,
                                 zx_thread_state_debug_regs_t*);

// Removes an installed execution HW breakpoint for |address|.
// If the address is not installed, no functional change will happen and
// ZX_ERR_OUT_OF_RANGE will be returned.
zx_status_t RemoveDebugBreakpoint(uint64_t address,
                                  zx_thread_state_debug_regs_t*);

// Useful function for debugging to keep around.
void PrintDebugRegisters(const zx_thread_state_debug_regs_t&);

}  // namespace arch
}  // namespace debug_agent

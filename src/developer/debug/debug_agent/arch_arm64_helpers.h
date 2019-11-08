// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_ARM64_HELPERS_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_ARM64_HELPERS_H_

#include <lib/zx/thread.h>
#include <zircon/syscalls/exception.h>

#include "src/developer/debug/debug_agent/arch_arm64.h"
#include "src/developer/debug/ipc/protocol.h"

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
std::string DebugRegistersToString(const zx_thread_state_debug_regs_t&);

// Given the current register value in |regs|, applies to it the new updated values for the
// registers listed in |updates|.
zx_status_t WriteGeneralRegisters(const std::vector<debug_ipc::Register>& update,
                                  zx_thread_state_general_regs_t* regs);
zx_status_t WriteFloatingPointRegisters(const std::vector<debug_ipc::Register>& update,
                                        zx_thread_state_fp_regs_t* regs);
zx_status_t WriteVectorRegisters(const std::vector<debug_ipc::Register>& update,
                                 zx_thread_state_vector_regs_t* regs);
zx_status_t WriteDebugRegisters(const std::vector<debug_ipc::Register>& update,
                                zx_thread_state_debug_regs_t* regs);

}  // namespace arch
}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_ARM64_HELPERS_H_

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

// Returns the state the debug registers should be if we added a execution HW
// breakpoint for |address|.
// Returns ZX_ERR_ALREADY_BOUND if |address| is already installed.
// Returns ZX_ERR_NO_RESOURCES if there are no registers left.
zx_status_t SetupHWBreakpoint(uint64_t address, zx_thread_state_debug_regs_t*);

// Removes an installed execution HW breakpoint for |address|.
// If the address is not installed, no functional change will happen and
// ZX_ERR_OUT_OF_RANGE will be returned.
zx_status_t RemoveHWBreakpoint(uint64_t address, zx_thread_state_debug_regs_t*);

// Updated the state the debug registers should be if we added a watchpoint for |address|. Returns
// wherther the operation was successful, and it if was, what register slot was updated.
//
// Address has to be correctly aligned according to |size|, otherwise ZX_ERR_OUT_OF_RANGE will be
// returned. The possible values for size are:
//
// 1: 1 byte aligned.
// 2: 2 byte aligned.
// 4: 4 byte aligned.
// 8: 8 byte aligned.
//
// Any other |size| values will return ZX_ERR_INVALID_ARGS.
//
// |type| must be a watchpoint type (kWrite or kReadWrite).
//
// Returns ZX_ERR_INVALID_ARGS if |type| is not a watchpoint type.
// Returns ZX_ERR_ALREADY_BOUND if the |address|/|size| pair is already set.
// Returns ZX_ERR_NO_RESOURCES if there are no registers left.
WatchpointInstallationResult SetupWatchpoint(zx_thread_state_debug_regs_t*,
                                             const debug_ipc::AddressRange& range,
                                             debug_ipc::BreakpointType type);

// Removes an installed execution watchpoint for |address|. If the address is not installed, no
// functional change will happen and ZX_ERR_NOT_FOUND will be returned.
zx_status_t RemoveWatchpoint(zx_thread_state_debug_regs_t*, const debug_ipc::AddressRange& range);

// Aligns the given address according to the watchpoint size.
uint64_t WatchpointAddressAlign(const debug_ipc::AddressRange&);

uint64_t GetWatchpointLength(uint64_t dr7, int slot);
uint32_t GetWatchpointRW(uint64_t dr7, int slot);

// Debug functions -------------------------------------------------------------

std::string GeneralRegistersToString(const zx_thread_state_general_regs&);

std::string DebugRegistersToString(const zx_thread_state_debug_regs_t&);

std::string DR6ToString(uint64_t dr6);

std::string DR7ToString(uint64_t dr7);

}  // namespace arch
}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_ARCH_X64_HELPERS_H_

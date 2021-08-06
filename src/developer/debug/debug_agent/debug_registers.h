// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUG_REGISTERS_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUG_REGISTERS_H_

#include <zircon/syscalls/debug.h>

#include <optional>

#include "src/developer/debug/debug_agent/watchpoint_info.h"
#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/shared/address_range.h"

namespace debug_agent {

// Wrapper around the debug thread registers to allow them to be accessed uniformly regardless
// of the platform.
class DebugRegisters {
 public:
  DebugRegisters() : regs_() {}
  explicit DebugRegisters(const zx_thread_state_debug_regs& r) : regs_(r) {}

  // Fills the given state the debug registers to what it should be if we added an execution HW
  // breakpoint for |address|. Return false if there are no registers left.
  bool SetHWBreakpoint(uint64_t address);

  // Removes an installed execution HW breakpoint for |address|. If the address is not installed, no
  // functional change will happen and false will be returned.
  bool RemoveHWBreakpoint(uint64_t address);

  // Update the debug registers to install the given watchpoint. The type must be a watchpoint type
  // (kWrite or kReadWrite).
  //
  // The watchpoint count should be the number of hardware watchpoints on the current system. It is
  // passed as a parameter here to allow this function to be tested under different conditions.
  //
  // The address has to be correctly aligned according to its length or an error will be returned.
  // The possible values for the size are:
  //
  //   size = 1: 1 byte aligned address.
  //   size = 2: 2 byte aligned address.
  //   size = 4: 4 byte aligned address.
  //   size = 8: 8 byte aligned address.
  //
  // Any other |size| values will return error.
  std::optional<WatchpointInfo> SetWatchpoint(debug_ipc::BreakpointType type,
                                              const debug::AddressRange& range,
                                              uint32_t watchpoint_count);

  // Updates the debug registers to remove an installed watchpoint for the given range. Returns
  // true on success, false if the range is not installed.
  bool RemoveWatchpoint(const debug::AddressRange& range, uint32_t watchpoint_count);

  // Decodes the debug registers given the state after a watchpoint exception has been thrown.
  std::optional<WatchpointInfo> DecodeHitWatchpoint() const;

  // Sets the debug registers to indicate a hit of the watchpoint of the given slot. This is used in
  // tests to set up calls for DecodeHitWatchpoint() to succeed.
  void SetForHitWatchpoint(int slot);

  std::string ToString() const;

  zx_thread_state_debug_regs& GetNativeRegisters() { return regs_; }
  const zx_thread_state_debug_regs& GetNativeRegisters() const { return regs_; }

 private:
  zx_thread_state_debug_regs regs_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUG_REGISTERS_H_

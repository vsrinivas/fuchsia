// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUG_REGISTERS_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUG_REGISTERS_H_

#include <zircon/syscalls/debug.h>

namespace debug_agent {

// Wrapper around the debug thread registers to allow them to be accessed uniformly regardless
// of the platform.
class DebugRegisters {
 public:
  DebugRegisters() : regs_() {}
  explicit DebugRegisters(const zx_thread_state_debug_regs& r) : regs_(r) {}

  // Appends the current general registers to the given high-level register record.
  void CopyTo(std::vector<debug_ipc::Register>& dest) const;

  zx_thread_state_debug_regs& GetNativeRegisters() { return regs_; }
  const zx_thread_state_debug_regs& GetNativeRegisters() const { return regs_; }

 private:
  zx_thread_state_debug_regs regs_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_DEBUG_REGISTERS_H_

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/syslog/cpp/macros.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/debug_registers.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

bool DebugRegisters::SetHWBreakpoint(uint64_t address) {
  return true;
}

bool DebugRegisters::RemoveHWBreakpoint(uint64_t address) {
  return true;
}

std::optional<WatchpointInfo> DebugRegisters::SetWatchpoint(debug_ipc::BreakpointType type,
                                                            const debug::AddressRange& range,
                                                            uint32_t watchpoint_count) {
  return std::nullopt;
}

bool DebugRegisters::RemoveWatchpoint(const debug::AddressRange& range,
                                      uint32_t watchpoint_count) {
  return false;
}

std::optional<WatchpointInfo> DebugRegisters::DecodeHitWatchpoint() const {
  return std::nullopt;
}

void DebugRegisters::SetForHitWatchpoint(int slot) {
}

std::string DebugRegisters::ToString() const {
  return "";
}

}  // namespace debug_agent

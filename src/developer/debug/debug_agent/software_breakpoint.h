// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/status.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/ipc/protocol.h"

namespace debug_agent {


class ProcessBreakpoint;
class ProcessMemoryAccessor;

class SoftwareBreakpoint {
 public:
  SoftwareBreakpoint(ProcessBreakpoint*, ProcessMemoryAccessor*);
  ~SoftwareBreakpoint();

  zx_status_t Install();
  void Uninstall();

  void FixupMemoryBlock(debug_ipc::MemoryBlock* block);

 private:
  ProcessBreakpoint* process_bp_;           // Not-owning.
  ProcessMemoryAccessor* memory_accessor_;  // Not-owning.

  // Set to true when the instruction has been replaced.
  bool installed_ = false;

  // Previous memory contents before being replaced with the break instruction.
  arch::BreakInstructionType previous_data_ = 0;
};

}  // namespace debug_agent

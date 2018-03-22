// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/debug_agent/arch.h"
#include "garnet/lib/debug_ipc/records.h"
#include "garnet/public/lib/fxl/macros.h"

class DebuggedProcess;

// Represents a breakpoint in the debug agent. These breakpoints are
// per-process (client breakpoints in the zxdb debugger can span multiple
// processes, and in that case will reference multiple ProcessBreakpoints).
class ProcessBreakpoint {
 public:
  // Call SetSettings immediately after construction.
  explicit ProcessBreakpoint(DebuggedProcess* process);
  ~ProcessBreakpoint();

  uint64_t address() const { return settings_.address; }

  // If SetSettings fails, this breakpoint is invalid and should be deleted.
  bool SetSettings(const debug_ipc::BreakpointSettings& settings);

  // Writing debug breakpoints changes memory contents. If an unmodified
  // virtual picture of memory is needed, this function will replace the
  // replacement from this breakpoint if it appears in the given block.
  // Otherwise does nothing.
  void FixupMemoryBlock(debug_ipc::MemoryBlock* block);

 private:
  // Install or uninstall this breakpoint.
  bool Install();
  void Uninstall();

  DebuggedProcess* process_;  // Backpointer to owner of this class.

  debug_ipc::BreakpointSettings settings_;

  // Set to true when the instruction has been replaced.
  bool installed_ = false;

  // Previous memory contents before being replaced with the break instruction.
  arch::BreakInstructionType previous_data_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(ProcessBreakpoint);
};

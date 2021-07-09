// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_HARDWARE_BREAKPOINT_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_HARDWARE_BREAKPOINT_H_

#include <set>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/process_breakpoint.h"

namespace debug_agent {

class HardwareBreakpoint : public ProcessBreakpoint {
 public:
  explicit HardwareBreakpoint(Breakpoint* breakpoint, DebuggedProcess* process, uint64_t address);
  virtual ~HardwareBreakpoint();

  debug::Status Update() override;

  debug_ipc::BreakpointType Type() const override { return debug_ipc::BreakpointType::kHardware; }
  bool Installed(zx_koid_t thread_koid) const override;

  const std::set<zx_koid_t>& installed_threads() const { return installed_threads_; }

  // Public ProcessBreakpoint overrides. See ProcessBreakpoint for more details.
  void EndStepOver(DebuggedThread* thread) override;
  void ExecuteStepOver(DebuggedThread* thread) override;

  void StepOverCleanup(DebuggedThread* thread) override {}

 private:
  debug::Status Install(DebuggedThread* thread);

  debug::Status Uninstall(DebuggedThread* thread) override;
  debug::Status Uninstall() override;

  std::set<zx_koid_t> installed_threads_;
  std::set<zx_koid_t> current_stepping_over_threads_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_HARDWARE_BREAKPOINT_H_

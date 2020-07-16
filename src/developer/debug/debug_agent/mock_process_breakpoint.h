// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_PROCESS_BREAKPOINT_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_PROCESS_BREAKPOINT_H_

#include "src/developer/debug/debug_agent/hardware_breakpoint.h"
#include "src/developer/debug/debug_agent/process_breakpoint.h"
#include "src/developer/debug/debug_agent/software_breakpoint.h"

namespace debug_agent {

// Base class for creating mocks for ProcessBreakpoint. This class implements the whole virtual
// interface of ProcessBreakpoints with sensibles no-ops, so you can go ahead and only override the
// functions that you need for your test.
class MockProcessBreakpoint : public ProcessBreakpoint {
 public:
  explicit MockProcessBreakpoint(Breakpoint* breakpoint, DebuggedProcess* process, uint64_t address,
                                 debug_ipc::BreakpointType type)
      : ProcessBreakpoint(breakpoint, process, address), type_(type) {}

  debug_ipc::BreakpointType Type() const override { return type_; }

  bool Installed(zx_koid_t thread_koid) const override { return false; }
  void BeginStepOver(DebuggedThread* thread) override {}
  void EndStepOver(DebuggedThread* thread) override {}
  void ExecuteStepOver(DebuggedThread* thread) override {}
  void StepOverCleanup(DebuggedThread* thread) override {}
  zx_status_t Update() override { return ZX_OK; }

 private:
  zx_status_t Uninstall(DebuggedThread* thread) override { return ZX_OK; }
  zx_status_t Uninstall() override { return ZX_OK; }

  debug_ipc::BreakpointType type_ = debug_ipc::BreakpointType::kLast;
};

class MockSoftwareBreakpoint : public SoftwareBreakpoint {
 public:
  explicit MockSoftwareBreakpoint(Breakpoint* breakpoint, DebuggedProcess* process,
                                  uint64_t address)
      : SoftwareBreakpoint(breakpoint, process, address) {}

  debug_ipc::BreakpointType Type() const override { return debug_ipc::BreakpointType::kSoftware; }
  bool Installed(zx_koid_t thread_koid) const override { return false; }
  void BeginStepOver(DebuggedThread* thread) override {}
  void EndStepOver(DebuggedThread* thread) override {}
  void ExecuteStepOver(DebuggedThread* thread) override {}
  void StepOverCleanup(DebuggedThread* thread) override {}
  zx_status_t Update() override { return ZX_OK; }

 private:
  zx_status_t Uninstall(DebuggedThread* thread) override { return ZX_OK; }
  zx_status_t Uninstall() override { return ZX_OK; }
};

class MockHardwareBreakpoint : public HardwareBreakpoint {
 public:
  explicit MockHardwareBreakpoint(Breakpoint* breakpoint, DebuggedProcess* process,
                                  uint64_t address)
      : HardwareBreakpoint(breakpoint, process, address) {}

  debug_ipc::BreakpointType Type() const override { return debug_ipc::BreakpointType::kHardware; }
  bool Installed(zx_koid_t thread_koid) const override { return false; }
  void BeginStepOver(DebuggedThread* thread) override {}
  void EndStepOver(DebuggedThread* thread) override {}
  void ExecuteStepOver(DebuggedThread* thread) override {}
  void StepOverCleanup(DebuggedThread* thread) override {}
  zx_status_t Update() override { return ZX_OK; }

 private:
  zx_status_t Uninstall(DebuggedThread* thread) override { return ZX_OK; }
  zx_status_t Uninstall() override { return ZX_OK; }
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_MOCK_PROCESS_BREAKPOINT_H_

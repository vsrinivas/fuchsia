// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_DEBUG_AGENT_SOFTWARE_BREAKPOINT_H_
#define SRC_DEVELOPER_DEBUG_DEBUG_AGENT_SOFTWARE_BREAKPOINT_H_

#include <zircon/status.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/process_breakpoint.h"
#include "src/developer/debug/debug_agent/suspend_handle.h"
#include "src/developer/debug/ipc/protocol.h"

namespace debug_agent {

class ProcessMemoryAccessor;

class SoftwareBreakpoint : public ProcessBreakpoint {
 public:
  SoftwareBreakpoint(Breakpoint* breakpoint, DebuggedProcess* process, uint64_t address);
  virtual ~SoftwareBreakpoint();

  debug_ipc::BreakpointType Type() const override { return debug_ipc::BreakpointType::kSoftware; }

  // Software breakpoint is either installed for all threads or no one.
  bool Installed(zx_koid_t thread_koid) const override { return installed_; }

  // virtual picture of memory is needed, this function will replace the replacement from this
  // breakpoint if it appears in the given block. Otherwise does nothing.
  void FixupMemoryBlock(debug_ipc::MemoryBlock* block);

  // Public ProcessBreakpoint overrides. See ProcessBreakpoint for more details.
  void EndStepOver(DebuggedThread* thread) override;
  void ExecuteStepOver(DebuggedThread* thread) override;
  void StepOverCleanup(DebuggedThread* thread) override;

  const DebuggedThread* currently_stepping_over_thread() const {
    return currently_stepping_over_thread_.get();
  }

  // Returns a sorted list of the koids associated with a currently held suspend token.
  // If a thread has more than one suspend token, it wil appear twice.
  //
  // Exposed mostly for testing purposes (see process_breakpoint_unittest.cc).
  std::vector<zx_koid_t> CurrentlySuspendedThreads() const;

 private:
  // ProcessBreakpoint overrides.
  debug::Status Update() override;

  // A software breakpoint gets uninstalled for all the threads.
  debug::Status Uninstall(DebuggedThread* thread) override { return Uninstall(); }
  debug::Status Uninstall() override;

  debug::Status Install();

  // As stepping over are queued, only one thread should be left running at a time. This makes the
  // breakpoint get a suspend token for each other thread within the system.
  void SuspendAllOtherThreads(zx_koid_t stepping_over_koid);

  // Set to true when the instruction has been replaced.
  bool installed_ = false;

  // Previous memory contents before being replaced with the break instruction.
  arch::BreakInstructionType previous_data_ = 0;

  // Tracks the threads currently single-stepping over this breakpoint.
  // There can be only one thread stepping over, as they're serialized by the process so that only
  // one thread is stepping at a time.
  fxl::WeakPtr<DebuggedThread> currently_stepping_over_thread_;

  // A step is executed by putting back the original instruction, stepping the thread, and then
  // re-inserting the breakpoint instruction. The breakpoint instruction can't be put back until
  // there are no more threads in this map.
  //
  // It is a multimap because if two threads are queued on the same breakpoint (they both hit it at
  // the same time), the breakpoint will get suspend tokens for all the threads (except the
  // corresponding exception one) multiple times. If there is only one suspend token per koid, the
  // breakpoint will uncorrectly resume the thread that just stepped over when the other would
  // step over too, which is incorrect. We need the ability to have multiple tokens associated to
  // a thread so that the interim between executing the second step over the same breakpoint can
  // coincide with waiting for the resources of the first step over to be freed.
  //
  // See the implementation of |StepOverCleanup| for more details.
  std::multimap<zx_koid_t, std::unique_ptr<SuspendHandle>> suspend_tokens_;
};

}  // namespace debug_agent

#endif  // SRC_DEVELOPER_DEBUG_DEBUG_AGENT_SOFTWARE_BREAKPOINT_H_

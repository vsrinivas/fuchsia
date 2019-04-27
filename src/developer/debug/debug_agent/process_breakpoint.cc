// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/process_breakpoint.h"

#include <inttypes.h>
#include <zircon/status.h>
#include <zircon/syscalls/exception.h>

#include "src/developer/debug/debug_agent/breakpoint.h"
#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/debug_agent/hardware_breakpoint.h"
#include "src/developer/debug/debug_agent/software_breakpoint.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

// ProcessBreakpoint Implementation --------------------------------------------

ProcessBreakpoint::ProcessBreakpoint(Breakpoint* breakpoint,
                                     DebuggedProcess* process,
                                     ProcessMemoryAccessor* memory_accessor,
                                     uint64_t address)
    : process_(process), memory_accessor_(memory_accessor), address_(address) {
  breakpoints_.push_back(breakpoint);
}

ProcessBreakpoint::~ProcessBreakpoint() { Uninstall(); }

zx_status_t ProcessBreakpoint::Init() { return Update(); }

zx_status_t ProcessBreakpoint::RegisterBreakpoint(Breakpoint* breakpoint) {
  // Shouldn't get duplicates.
  FXL_DCHECK(std::find(breakpoints_.begin(), breakpoints_.end(), breakpoint) ==
             breakpoints_.end());
  breakpoints_.push_back(breakpoint);
  // Check if we need to install/uninstall a breakpoint.
  return Update();
}

bool ProcessBreakpoint::UnregisterBreakpoint(Breakpoint* breakpoint) {
  auto found = std::find(breakpoints_.begin(), breakpoints_.end(), breakpoint);
  if (found == breakpoints_.end()) {
    FXL_NOTREACHED();  // Should always be found.
  } else {
    breakpoints_.erase(found);
  }
  // Check if we need to install/uninstall a breakpoint.
  Update();
  return !breakpoints_.empty();
}

void ProcessBreakpoint::FixupMemoryBlock(debug_ipc::MemoryBlock* block) {
  if (software_breakpoint_)
    software_breakpoint_->FixupMemoryBlock(block);
}

bool ProcessBreakpoint::ShouldHitThread(zx_koid_t thread_koid) const {
  for (const Breakpoint* bp : breakpoints_) {
    if (bp->AppliesToThread(process_->koid(), thread_koid))
      return true;
  }

  return false;
}

void ProcessBreakpoint::OnHit(
    debug_ipc::BreakpointType exception_type,
    std::vector<debug_ipc::BreakpointStats>* hit_breakpoints) {
  hit_breakpoints->clear();
  for (Breakpoint* breakpoint : breakpoints_) {
    // Only care for breakpoints that match the exception type.
    if (breakpoint->type() != exception_type)
      continue;

    breakpoint->OnHit();
    hit_breakpoints->push_back(breakpoint->stats());
  }
}

void ProcessBreakpoint::BeginStepOver(zx_koid_t thread_koid) {
  // Shouldn't be recursively stepping over a breakpoint from the same thread.
  FXL_DCHECK(thread_step_over_.find(thread_koid) == thread_step_over_.end());

  if (!CurrentlySteppingOver()) {
    // This is the first thread to attempt to step over the breakpoint (there
    // could theoretically be more than one).
    Uninstall();
  }
  thread_step_over_[thread_koid] = StepStatus::kCurrent;
}

bool ProcessBreakpoint::BreakpointStepHasException(
    zx_koid_t thread_koid, debug_ipc::NotifyException::Type exception_type) {
  auto found_thread = thread_step_over_.find(thread_koid);
  if (found_thread == thread_step_over_.end()) {
    // Shouldn't be getting these notifications from a thread not currently
    // doing a step-over.
    FXL_NOTREACHED();
    return false;
  }
  StepStatus step_status = found_thread->second;
  thread_step_over_.erase(found_thread);

  // When the last thread is done stepping over, put the breakpoint back.
  if (step_status == StepStatus::kCurrent && !CurrentlySteppingOver())
    Update();

  // Now check if this exception was likely caused by successfully stepping
  // over the breakpoint, or something else (the stepped
  // instruction crashed or something).
  return exception_type == debug_ipc::NotifyException::Type::kSingleStep;
}

bool ProcessBreakpoint::SoftwareBreakpointInstalled() const {
  return software_breakpoint_ != nullptr;
}

bool ProcessBreakpoint::HardwareBreakpointInstalled() const {
  return hardware_breakpoint_ != nullptr &&
         !hardware_breakpoint_->installed_threads().empty();
}

bool ProcessBreakpoint::CurrentlySteppingOver() const {
  for (const auto& pair : thread_step_over_) {
    if (pair.second == StepStatus::kCurrent)
      return true;
  }
  return false;
}

zx_status_t ProcessBreakpoint::Update() {
  // Software breakpoints remain installed as long as even one remains active,
  // regardless of which threads are targeted.
  int sw_bp_count = 0;
  for (Breakpoint* bp : breakpoints_) {
    if (bp->type() == debug_ipc::BreakpointType::kSoftware)
      sw_bp_count++;
  }

  if (sw_bp_count == 0 && software_breakpoint_) {
    software_breakpoint_.reset();
  } else if (sw_bp_count > 0 && !software_breakpoint_) {
    software_breakpoint_ =
        std::make_unique<SoftwareBreakpoint>(this, memory_accessor_);
    zx_status_t status = software_breakpoint_->Install();
    if (status != ZX_OK)
      return status;
  }

  // Hardware breakpoints are different. We need to remove for all the threads
  // that are not covered anymore.
  std::set<zx_koid_t> threads = HWThreadsTargeted(*this);

  if (threads.empty()) {
    hardware_breakpoint_.reset();
  } else {
    if (!hardware_breakpoint_) {
      hardware_breakpoint_ = std::make_unique<HardwareBreakpoint>(this);
    }
    return hardware_breakpoint_->Update(threads);
  }

  return ZX_OK;
}

void ProcessBreakpoint::Uninstall() {
  software_breakpoint_.reset();
  hardware_breakpoint_.reset();
}

}  // namespace debug_agent

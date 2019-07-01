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

namespace {

std::string Preamble(ProcessBreakpoint* b) {
  return fxl::StringPrintf("[Breakpoint 0x%zx] ", b->address());
}

void LogThreadsSteppingOver(ProcessBreakpoint* b, const std::set<zx_koid_t>& thread_koids) {
  if (!debug_ipc::IsDebugModeActive())
    return;

  std::stringstream ss;
  ss << "Current threads stepping over: ";
  int count = 0;
  for (zx_koid_t thread_koid : thread_koids) {
    if (count++ > 0)
      ss << ", ";
    ss << thread_koid;
  }

  DEBUG_LOG(Breakpoint) << Preamble(b) << ss.str();
}

void SuspendAllOtherNonSteppingOverThreads(ProcessBreakpoint* b, DebuggedProcess* process,
                                           zx_koid_t thread_koid) {
  std::vector<DebuggedThread*> suspended_threads;
  for (DebuggedThread* thread : process->GetThreads()) {
    if (thread->stepping_over_breakpoint()) {
      DEBUG_LOG(Breakpoint) << Preamble(b) << "Thread " << thread->koid()
                            << " is currently stepping over.";
      continue;
    }

    // We async suspend all the threads and then wait for all of them to signal.
    if (thread->IsSuspended()) {
      DEBUG_LOG(Breakpoint) << Preamble(b) << "Thread " << thread->koid()
                            << " is already suspended.";
      continue;
    }

    thread->Suspend(false);   // Async suspend.
  }

  // We wait on all the suspend signals to trigger.
  for (DebuggedThread* thread : suspended_threads) {
    bool suspended = thread->WaitForSuspension();
    FXL_DCHECK(suspended) << "Thread " << thread_koid;
  }
}

}  // namespace

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

  FXL_DCHECK(!CurrentlySteppingOver(thread_koid));
  DebuggedThread* thread = process_->GetThread(thread_koid);
  FXL_DCHECK(thread);
  FXL_DCHECK(!thread->stepping_over_breakpoint());
  thread->set_stepping_over_breakpoint(true);

  DEBUG_LOG(Breakpoint) << Preamble(this) << "Thread " << thread_koid << " is stepping over.";

  auto [_, inserted] = threads_stepping_over_.insert(thread_koid);
  FXL_DCHECK(inserted);

  LogThreadsSteppingOver(this, threads_stepping_over_);

  SuspendAllOtherNonSteppingOverThreads(this, process_, thread_koid);

  // If this is the first thread attempting to step over, we uninstall it.
  if (threads_stepping_over_.size() == 1u)
    Uninstall();

  // This thread now has to continue running.
  thread->ResumeException();
  thread->ResumeSuspension();
}

void ProcessBreakpoint::EndStepOver(zx_koid_t thread_koid) {
  FXL_DCHECK(CurrentlySteppingOver(thread_koid));
  DebuggedThread* thread = process_->GetThread(thread_koid);
  FXL_DCHECK(thread);
  FXL_DCHECK(thread->stepping_over_breakpoint());
  thread->set_stepping_over_breakpoint(false);

  threads_stepping_over_.erase(thread_koid);

  DEBUG_LOG(Breakpoint) << Preamble(this) << "Thread " << thread_koid << " ending step over.";
  LogThreadsSteppingOver(this, threads_stepping_over_);


  // If all the threads have stepped over, we reinstall the breakpoint and
  // resume all threads.
  if (!CurrentlySteppingOver()) {
    DEBUG_LOG(Breakpoint) << Preamble(this) << "No more threads left. Resuming.";
    Update();
    for (DebuggedThread* thread : process_->GetThreads()) {
      thread->ResumeForRunMode();
    }

    return;
  }

  // Otherwise this thread needs to wait until all other threads are done
  // stepping over.
  thread->Suspend();
}

bool ProcessBreakpoint::SoftwareBreakpointInstalled() const {
  return software_breakpoint_ != nullptr;
}

bool ProcessBreakpoint::HardwareBreakpointInstalled() const {
  return hardware_breakpoint_ != nullptr &&
         !hardware_breakpoint_->installed_threads().empty();
}

bool ProcessBreakpoint::CurrentlySteppingOver(zx_koid_t thread_koid) const {
  if (thread_koid == 0)
    return !threads_stepping_over_.empty();

  auto it = threads_stepping_over_.find(thread_koid);
  return it != threads_stepping_over_.end();
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

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

namespace {

std::string Preamble(ProcessBreakpoint* b) {
  std::stringstream ss;

  ss << "[PB 0x" << std::hex << b->address();
  bool first = true;

  // Add the names of all the breakpoints associated with this process breakpoint.
  ss << " (";
  for (Breakpoint* breakpoint : b->breakpoints()) {
    if (!first) {
      first = false;
      ss << ", ";
    }
    ss << breakpoint->settings().name;
  }

  ss << ")] ";
  return ss.str();
}

}  // namespace

ProcessBreakpoint::ProcessBreakpoint(Breakpoint* breakpoint, DebuggedProcess* process,
                                     uint64_t address)
    : process_(process), address_(address), weak_factory_(this) {
  breakpoints_.push_back(breakpoint);
}

ProcessBreakpoint::~ProcessBreakpoint() = default;

zx_status_t ProcessBreakpoint::Init() { return Update(); }

fxl::WeakPtr<ProcessBreakpoint> ProcessBreakpoint::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

zx_status_t ProcessBreakpoint::RegisterBreakpoint(Breakpoint* breakpoint) {
  // Shouldn't get duplicates.
  FXL_DCHECK(std::find(breakpoints_.begin(), breakpoints_.end(), breakpoint) == breakpoints_.end());

  // Should be the same type.
  FXL_DCHECK(Type() == breakpoint->type());

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

bool ProcessBreakpoint::ShouldHitThread(zx_koid_t thread_koid) const {
  for (const Breakpoint* bp : breakpoints_) {
    if (bp->AppliesToThread(process_->koid(), thread_koid))
      return true;
  }

  return false;
}

void ProcessBreakpoint::OnHit(debug_ipc::BreakpointType exception_type,
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

void ProcessBreakpoint::BeginStepOver(DebuggedThread* thread) {
  process_->EnqueueStepOver(this, thread);
}

void ProcessBreakpoint::ExecuteStepOver(DebuggedThread* thread) {
  DEBUG_LOG(Breakpoint) << Preamble(this) << "Thread " << thread->koid() << " is stepping over.";
  currently_stepping_over_thread_ = thread->GetWeakPtr();
  thread->set_stepping_over_breakpoint(true);

  SuspendAllOtherThreads(thread->koid());

  Uninstall(thread);

  // This thread now has to continue running.
  thread->ResumeException();
  thread->ResumeSuspension();
}

void ProcessBreakpoint::EndStepOver(DebuggedThread* thread) {
  FXL_DCHECK(thread->stepping_over_breakpoint());
  FXL_DCHECK(currently_stepping_over_thread_);
  FXL_DCHECK(currently_stepping_over_thread_->koid() == thread->koid())
      << " Expected " << currently_stepping_over_thread_->koid() << ", Got " << thread->koid();

  DEBUG_LOG(Breakpoint) << Preamble(this) << "Thread " << thread->koid() << " ending step over.";
  thread->set_stepping_over_breakpoint(false);
  currently_stepping_over_thread_.reset();

  // Install the breakpoint again.
  // NOTE(donosoc): For multiple threads stepping over (queue), this is inefficient as threads are
  //                suspended and there is no need to reinstall them every time, expect for
  //                implementation simplicity. If performance becomes an issue, we could create a
  //                notification that the process calls when the complete step queue has been done
  //                that tells the breakpoints to reinstall themselves.
  Update();

  // Tell the process we're done stepping over.
  process_->OnBreakpointFinishedSteppingOver();
}

void ProcessBreakpoint::StepOverCleanup(DebuggedThread* thread) {
  DEBUG_LOG(Breakpoint) << Preamble(this) << "Finishing step over for thread " << thread->koid();

  // We are done stepping over this thread, so we can remove the suspend tokens. Normally this means
  // cleaning all the suspend tokens, if there is only one thread in the stepping over queue or the
  // next step over is another breakpoint.
  //
  // But in the case that another thread is stepping over *the same* breakpoint, cleaning all the
  // tokens would resume all the threads that have just been suspended by the next instance of the
  // step over.
  //
  // For this case we need the ability to maintain more than one suspend tokens per thread: one for
  // the first step over and another for the second, as they coincide between the process callind
  // |ExecuteStepOver| on the second instance and callind |StepOverCleanup| on the first one.
  auto it = suspend_tokens_.begin();
  while (it != suspend_tokens_.end()) {
    // Calculate the upper bound (so we skip over repeated keys).
    auto cur_it = it;
    it = suspend_tokens_.upper_bound(it->first);

    // We do not erase a token for the thread we just stepped over, because it will be the only
    // thread that will not have 2 suspend tokens: It will have the one taken by the next step over,
    // as the first one didn't get one.
    if (cur_it->first == thread->koid())
      continue;

    // All other threads would have 2 suspend tokens (one for the first step over and one for the
    // second), meaning that we can safely remove the first one.
    suspend_tokens_.erase(cur_it);
  }

  // Remote the thread from the exception.
  thread->ResumeException();
}

void ProcessBreakpoint::SuspendAllOtherThreads(zx_koid_t stepping_over_koid) {
  std::vector<DebuggedThread*> suspended_threads;
  for (DebuggedThread* thread : process_->GetThreads()) {
    // We do not suspend the stepping over thread.
    if (thread->koid() == stepping_over_koid)
      continue;

    // Only one thread should be stepping over at a time.
    if (thread->stepping_over_breakpoint() && thread->koid() != stepping_over_koid) {
      FXL_NOTREACHED() << "Thread " << thread->koid() << " is stepping over. Only thread "
                       << stepping_over_koid << " should be stepping over.";
    }

    // We keep every other thread suspended.
    // If this is a re-entrant breakpoint (two threads in a row are stepping over the same
    // breakpoint), we could have more than one token for each thread.
    suspend_tokens_.insert({thread->koid(), thread->RefCountedSuspend(false)});
  }

  // We wait on all the suspend signals to trigger.
  for (DebuggedThread* thread : suspended_threads) {
    bool suspended = thread->WaitForSuspension();
    FXL_DCHECK(suspended) << "Thread " << thread->koid();
  }
}

std::vector<zx_koid_t> ProcessBreakpoint::CurrentlySuspendedThreads() const {
  std::vector<zx_koid_t> koids;
  koids.reserve(suspend_tokens_.size());
  for (auto& [thread_koid, _] : suspend_tokens_) {
    koids.emplace_back(thread_koid);
  }

  std::sort(koids.begin(), koids.end());
  return koids;
}

}  // namespace debug_agent

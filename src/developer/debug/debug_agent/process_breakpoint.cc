// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/process_breakpoint.h"

#include <inttypes.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/syscalls/exception.h>

#include "src/developer/debug/debug_agent/breakpoint.h"
#include "src/developer/debug/debug_agent/debug_agent.h"
#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/debug_agent/hardware_breakpoint.h"
#include "src/developer/debug/debug_agent/software_breakpoint.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

ProcessBreakpoint::ProcessBreakpoint(Breakpoint* breakpoint, DebuggedProcess* process,
                                     uint64_t address)
    : process_(process), address_(address), weak_factory_(this) {
  breakpoints_.push_back(breakpoint);
}

ProcessBreakpoint::~ProcessBreakpoint() = default;

debug::Status ProcessBreakpoint::Init() { return Update(); }

fxl::WeakPtr<ProcessBreakpoint> ProcessBreakpoint::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

debug::Status ProcessBreakpoint::RegisterBreakpoint(Breakpoint* breakpoint) {
  // Shouldn't get duplicates.
  if (std::find(breakpoints_.begin(), breakpoints_.end(), breakpoint) != breakpoints_.end())
    return debug::Status("Breakpoint already registered");

  // Should be the same type.
  if (Type() != breakpoint->settings().type)
    return debug::Status("Breakpoint should be the same type");

  breakpoints_.push_back(breakpoint);
  // Check if we need to install/uninstall a breakpoint.
  return Update();
}

bool ProcessBreakpoint::UnregisterBreakpoint(Breakpoint* breakpoint) {
  DEBUG_LOG(Breakpoint) << "Unregistering breakpoint " << breakpoint->settings().id << " ("
                        << breakpoint->settings().name << ").";

  auto found = std::find(breakpoints_.begin(), breakpoints_.end(), breakpoint);
  if (found == breakpoints_.end()) {
    FX_NOTREACHED();  // Should always be found.
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

void ProcessBreakpoint::OnHit(DebuggedThread* hitting_thread,
                              debug_ipc::BreakpointType exception_type,
                              std::vector<debug_ipc::BreakpointStats>& hit_breakpoints,
                              std::vector<debug_ipc::ThreadRecord>& other_affected_threads) {
  // This will be filled in with the largest scope to stop.
  debug_ipc::Stop max_stop = debug_ipc::Stop::kNone;

  DebugAgent* agent = process_->debug_agent();

  // How much stack to capture for the suspended threads.
  constexpr auto kSuspendedStackAmount = debug_ipc::ThreadRecord::StackAmount::kMinimal;

  hit_breakpoints.clear();
  for (Breakpoint* breakpoint : breakpoints_) {
    // Only care for breakpoints that match the exception type.
    if (!Breakpoint::DoesExceptionApply(breakpoint->settings().type, exception_type))
      continue;

    breakpoint->OnHit();

    // The breakpoint stats are for the client, don't tell it about our internal ones.
    if (!breakpoint->is_debug_agent_internal())
      hit_breakpoints.push_back(breakpoint->stats());

    if (static_cast<uint32_t>(breakpoint->settings().stop) > static_cast<uint32_t>(max_stop))
      max_stop = breakpoint->settings().stop;
  }

  // Apply the maximal stop mode.
  switch (max_stop) {
    case debug_ipc::Stop::kNone: {
      // In this case the client will be in charge of resuming the thread because it may need to do
      // stuff like printing a message.
      break;
    }
    case debug_ipc::Stop::kThread: {
      // The thread is already stopped, nothing to do.
      break;
    }
    case debug_ipc::Stop::kProcess: {
      // Suspend each thread in the process except the one that just hit the exception (leave it
      // suspended in the exception).
      std::vector<debug_ipc::ProcessThreadId> suspended_ids =
          process_->ClientSuspendAllThreads(hitting_thread->koid());

      // Save the record for each suspended thread.
      for (const debug_ipc::ProcessThreadId& id : suspended_ids) {
        if (DebuggedThread* thread = process_->GetThread(id.thread))
          other_affected_threads.push_back(thread->GetThreadRecord(kSuspendedStackAmount));
      }
      break;
    }
    case debug_ipc::Stop::kAll: {
      // Suspend each thread in all processes except the one that just hit the exception (leave it
      // suspended in the exception).
      std::vector<debug_ipc::ProcessThreadId> proc_thread_pairs =
          agent->ClientSuspendAll(process_->koid(), hitting_thread->koid());

      for (const debug_ipc::ProcessThreadId& id : proc_thread_pairs) {
        if (DebuggedThread* thread = agent->GetDebuggedThread(id))
          other_affected_threads.push_back(thread->GetThreadRecord(kSuspendedStackAmount));
      }
      break;
    }
  }
}

void ProcessBreakpoint::BeginStepOver(DebuggedThread* thread) {
  // Note that this request may get silently dropped in some edge cases (see EnqueueStepOver
  // comment) so don't keep any state about this request.
  process_->EnqueueStepOver(this, thread);
}

}  // namespace debug_agent

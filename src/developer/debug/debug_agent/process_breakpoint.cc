// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/process_breakpoint.h"

#include <inttypes.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/syscalls/exception.h>

#include "src/developer/debug/debug_agent/breakpoint.h"
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

zx_status_t ProcessBreakpoint::Init() { return Update(); }

fxl::WeakPtr<ProcessBreakpoint> ProcessBreakpoint::GetWeakPtr() {
  return weak_factory_.GetWeakPtr();
}

zx_status_t ProcessBreakpoint::RegisterBreakpoint(Breakpoint* breakpoint) {
  // Shouldn't get duplicates.
  if (std::find(breakpoints_.begin(), breakpoints_.end(), breakpoint) != breakpoints_.end())
    return ZX_ERR_ALREADY_BOUND;

  // Should be the same type.
  if (Type() != breakpoint->settings().type)
    return ZX_ERR_INVALID_ARGS;

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

void ProcessBreakpoint::OnHit(debug_ipc::BreakpointType exception_type,
                              std::vector<debug_ipc::BreakpointStats>* hit_breakpoints) {
  hit_breakpoints->clear();
  for (Breakpoint* breakpoint : breakpoints_) {
    // Only care for breakpoints that match the exception type.
    if (!Breakpoint::DoesExceptionApply(breakpoint->settings().type, exception_type))
      continue;

    breakpoint->OnHit();

    // The breakpoint stats are for the client, don't tell it about our internal ones.
    if (!breakpoint->is_debug_agent_internal())
      hit_breakpoints->push_back(breakpoint->stats());
  }
}

void ProcessBreakpoint::BeginStepOver(DebuggedThread* thread) {
  // Note that this request may get silently dropped in some edge cases (see EnqueueStepOver
  // comment) so don't keep any state about this request.
  process_->EnqueueStepOver(this, thread);
}

}  // namespace debug_agent

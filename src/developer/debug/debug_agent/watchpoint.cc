// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/watchpoint.h"

#include <zircon/status.h>

#include "src/developer/debug/debug_agent/breakpoint.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

namespace {

std::string LogPreamble(ProcessBreakpoint* b) {
  std::stringstream ss;

  ss << "[WP 0x" << std::hex << b->address();
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

enum class WarningType {
  kInstall,
  kUninstall,
};

void Warn(debug::FileLineFunction origin, WarningType type, zx_koid_t thread_koid,
          uint64_t address) {
  const char* verb = type == WarningType::kInstall ? "install" : "uninstall";
  printf("[%s:%d][%s] Could not %s HW breakpoint for thread %u at %" PRIX64, origin.file().c_str(),
         origin.line(), origin.function().c_str(), verb, static_cast<uint32_t>(thread_koid),
         address);

  fflush(stdout);
}

std::set<zx_koid_t> ThreadsTargeted(const Watchpoint& watchpoint) {
  std::set<zx_koid_t> ids;
  bool all_threads = false;
  for (Breakpoint* bp : watchpoint.breakpoints()) {
    // We only care about breakpoint that cover our case.
    if (!Breakpoint::DoesExceptionApply(watchpoint.Type(), bp->settings().type))
      continue;

    for (auto& location : bp->settings().locations) {
      // We only install for locations that match this process breakpoint.
      if (location.address_range != watchpoint.range())
        continue;

      auto thread_id = location.id.thread;
      if (thread_id == 0) {
        all_threads = true;
        break;
      } else {
        ids.insert(thread_id);
      }
    }

    // No need to continue searching if a breakpoint wants all threads.
    if (all_threads)
      break;
  }

  // If all threads are required, add them all.
  if (all_threads) {
    for (DebuggedThread* thread : watchpoint.process()->GetThreads())
      ids.insert(thread->koid());
  }

  return ids;
}

}  // namespace

Watchpoint::Watchpoint(debug_ipc::BreakpointType type, Breakpoint* breakpoint,
                       DebuggedProcess* process, const debug::AddressRange& range)
    : ProcessBreakpoint(breakpoint, process, range.begin()), type_(type), range_(range) {
  FX_DCHECK(IsWatchpointType(type))
      << "Wrong breakpoint type: " << debug_ipc::BreakpointTypeToString(type);
}

Watchpoint::~Watchpoint() { Uninstall(); }

bool Watchpoint::Installed(zx_koid_t thread_koid) const {
  return installed_threads_.count(thread_koid) > 0;
}

bool Watchpoint::MatchesException(zx_koid_t thread_koid, uint64_t watchpoint_address, int slot) {
  for (auto& [tid, installation] : installed_threads_) {
    if (tid != thread_koid)
      continue;

    if (installation.slot != slot)
      continue;

    if (installation.range.InRange(watchpoint_address))
      return true;
  }

  return false;
}

// ProcessBreakpoint Implementation ----------------------------------------------------------------

void Watchpoint::ExecuteStepOver(DebuggedThread* thread) {
  FX_DCHECK(current_stepping_over_threads_.count(thread->koid()) == 0);
  FX_DCHECK(!thread->stepping_over_breakpoint());

  DEBUG_LOG(Watchpoint) << LogPreamble(this) << "Thread " << thread->koid() << " is stepping over.";
  thread->set_stepping_over_breakpoint(true);
  current_stepping_over_threads_.insert(thread->koid());

  // HW breakpoints don't need to suspend any threads.
  Uninstall(thread);

  // The thread now can continue with the step over.
  thread->InternalResumeException();
}

void Watchpoint::EndStepOver(DebuggedThread* thread) {
  FX_DCHECK(thread->stepping_over_breakpoint());
  FX_DCHECK(current_stepping_over_threads_.count(thread->koid()) > 0);

  DEBUG_LOG(Watchpoint) << LogPreamble(this) << "Thread " << thread->koid() << " ending step over.";

  thread->set_stepping_over_breakpoint(false);
  current_stepping_over_threads_.erase(thread->koid());

  // We reinstall this breakpoint for the thread.
  Install(thread);

  // Tell the process we're done stepping over.
  process_->OnBreakpointFinishedSteppingOver();
}

// Update ------------------------------------------------------------------------------------------

debug::Status Watchpoint::Update() {
  // We get a snapshot of which threads are already installed.
  auto current_installs = installed_threads_;
  auto koids_to_install = ThreadsTargeted(*this);

  // Uninstall pass.
  for (auto& [thread_koid, installation] : current_installs) {
    if (koids_to_install.count(thread_koid) > 0)
      continue;

    // The ProcessBreakpoint not longer tracks this. Remove.
    DebuggedThread* thread = process()->GetThread(thread_koid);
    if (thread) {
      if (Uninstall(thread).has_error())
        continue;

      installed_threads_.erase(thread_koid);
    }
  }

  // Install pass.
  for (zx_koid_t thread_koid : koids_to_install) {
    // If it's already installed, ignore.
    if (installed_threads_.count(thread_koid) > 0)
      continue;

    DebuggedThread* thread = process()->GetThread(thread_koid);
    if (!thread)
      continue;

    if (!Install(thread))
      continue;
  }

  return debug::Status();
}

// Install -----------------------------------------------------------------------------------------

bool Watchpoint::Install(DebuggedThread* thread) {
  if (!thread)
    return false;

  DEBUG_LOG(Watchpoint) << "Installing watchpoint on thread " << thread->koid() << " on address 0x"
                        << std::hex << address();

  auto suspend_token = thread->InternalSuspend(true);

  // Do the actual installation.
  auto result = thread->thread_handle().InstallWatchpoint(type_, range_);
  if (!result) {
    Warn(FROM_HERE, WarningType::kInstall, thread->koid(), address());
    return false;
  }

  installed_threads_[thread->koid()] = *result;
  return true;
}

// Uninstall ---------------------------------------------------------------------------------------

debug::Status Watchpoint::Uninstall() {
  std::vector<zx_koid_t> uninstalled_threads;
  for (auto& [thread_koid, installation] : installed_threads_) {
    DebuggedThread* thread = process()->GetThread(thread_koid);
    if (!thread)
      continue;

    if (Uninstall(thread).has_error())
      continue;

    uninstalled_threads.push_back(thread_koid);
  }

  // Remove them from the list.
  for (zx_koid_t thread_koid : uninstalled_threads) {
    installed_threads_.erase(thread_koid);
  }

  return debug::Status();
}

debug::Status Watchpoint::Uninstall(DebuggedThread* thread) {
  if (!thread)
    return debug::Status("Thread expected for uninstalling watchpoint.");

  DEBUG_LOG(Watchpoint) << "Removing watchpoint on thread " << thread->koid() << " on address 0x"
                        << std::hex << address();

  auto suspend_token = thread->InternalSuspend(true);

  if (!thread->thread_handle().UninstallWatchpoint(range_)) {
    Warn(FROM_HERE, WarningType::kUninstall, thread->koid(), address());
    return debug::Status("Unable to uninstall watchpoint.");
  }

  return debug::Status();
}

}  // namespace debug_agent

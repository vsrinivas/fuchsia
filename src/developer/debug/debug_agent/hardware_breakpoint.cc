// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/hardware_breakpoint.h"

#include <inttypes.h>
#include <zircon/status.h>

#include "src/developer/debug/debug_agent/breakpoint.h"
#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/debug_agent/process_breakpoint.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace debug_agent {

namespace {

std::string LogPreamble(ProcessBreakpoint* b) {
  std::stringstream ss;

  ss << "[HW BP 0x" << std::hex << b->address();
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

void Warn(const debug::FileLineFunction& origin, WarningType type, zx_koid_t thread_koid,
          uint64_t address) {
  const char* verb = type == WarningType::kInstall ? "install" : "uninstall";
  DEBUG_LOG_WITH_LOCATION(Breakpoint, origin) << fxl::StringPrintf(
      "Could not %s HW breakpoint for thread %u at "
      "%" PRIX64,
      verb, static_cast<uint32_t>(thread_koid), address);
}

std::set<zx_koid_t> HWThreadsTargeted(const ProcessBreakpoint& pb) {
  std::set<zx_koid_t> ids;
  bool all_threads = false;
  for (Breakpoint* bp : pb.breakpoints()) {
    // We only care about hardware breakpoints.
    if (bp->settings().type != debug_ipc::BreakpointType::kHardware)
      continue;

    for (auto& location : bp->settings().locations) {
      // We only install for locations that match this process breakpoint.
      if (location.address != pb.address())
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
    for (DebuggedThread* thread : pb.process()->GetThreads())
      ids.insert(thread->koid());
  }

  return ids;
}

}  // namespace

HardwareBreakpoint::HardwareBreakpoint(Breakpoint* breakpoint, DebuggedProcess* process,
                                       uint64_t address)
    : ProcessBreakpoint(breakpoint, process, address) {}

HardwareBreakpoint::~HardwareBreakpoint() { Uninstall(); }

bool HardwareBreakpoint::Installed(zx_koid_t thread_koid) const {
  return installed_threads_.count(thread_koid) > 0;
}

// ProcessBreakpoint Implementation ----------------------------------------------------------------

void HardwareBreakpoint::ExecuteStepOver(DebuggedThread* thread) {
  FX_DCHECK(current_stepping_over_threads_.count(thread->koid()) == 0);
  FX_DCHECK(!thread->stepping_over_breakpoint());

  DEBUG_LOG(Breakpoint) << LogPreamble(this) << "Thread " << thread->koid() << " is stepping over.";
  thread->set_stepping_over_breakpoint(true);
  current_stepping_over_threads_.insert(thread->koid());

  // HW breakpoints don't need to suspend any threads.
  Uninstall(thread);

  // The thread now can continue with the step over.
  thread->InternalResumeException();
}

void HardwareBreakpoint::EndStepOver(DebuggedThread* thread) {
  FX_DCHECK(thread->stepping_over_breakpoint());
  FX_DCHECK(current_stepping_over_threads_.count(thread->koid()) > 0);

  DEBUG_LOG(Breakpoint) << LogPreamble(this) << "Thread " << thread->koid() << " ending step over.";

  thread->set_stepping_over_breakpoint(false);
  current_stepping_over_threads_.erase(thread->koid());

  // We reinstall this breakpoint for the thread.
  Install(thread);

  // Tell the process we're done stepping over.
  process_->OnBreakpointFinishedSteppingOver();
}

// Update ------------------------------------------------------------------------------------------

debug::Status HardwareBreakpoint::Update() {
  // We get a snapshot of which threads are already installed.
  auto current_koids = installed_threads_;
  auto koids_to_install = HWThreadsTargeted(*this);

  // Uninstall pass.
  for (zx_koid_t thread_koid : current_koids) {
    if (koids_to_install.count(thread_koid) > 0)
      continue;

    // The ProcessBreakpoint not longer tracks this. Remove.
    DebuggedThread* thread = process()->GetThread(thread_koid);
    if (thread) {
      if (Uninstall(thread).has_error())
        continue;
    }

    installed_threads_.erase(thread_koid);
  }

  // Install pass.
  for (zx_koid_t thread_koid : koids_to_install) {
    // If it's already installed, ignore.
    if (installed_threads_.count(thread_koid) > 0)
      continue;

    DebuggedThread* thread = process()->GetThread(thread_koid);
    if (!thread)
      continue;

    if (Install(thread).has_error())
      continue;

    installed_threads_.insert(thread_koid);
  }

  return debug::Status();
}

// Install -----------------------------------------------------------------------------------------

debug::Status HardwareBreakpoint::Install(DebuggedThread* thread) {
  if (!thread)
    return debug::Status("No thread provided for hardware breakpoint.");

  DEBUG_LOG(Breakpoint) << "Installing HW breakpoint on thread " << thread->koid()
                        << " on address 0x" << std::hex << address();

  auto suspend_token = thread->InternalSuspend(true);

  // Do the actual installation.
  if (!thread->thread_handle().InstallHWBreakpoint(address())) {
    Warn(FROM_HERE, WarningType::kInstall, thread->koid(), address());
    return debug::Status("Could not install hardware breakpoint.");
  }

  return debug::Status();
}

// Uninstall ---------------------------------------------------------------------------------------

debug::Status HardwareBreakpoint::Uninstall() {
  for (zx_koid_t thread_koid : installed_threads_) {
    DebuggedThread* thread = process()->GetThread(thread_koid);
    if (!thread)
      continue;

    if (Uninstall(thread).has_error())
      continue;
  }

  return debug::Status();
}

debug::Status HardwareBreakpoint::Uninstall(DebuggedThread* thread) {
  if (!thread)
    return debug::Status("Thread not found when uninstalling hardware breakpoint.");

  DEBUG_LOG(Breakpoint) << "Removing HW breakpoint on thread " << thread->koid() << " on address 0x"
                        << std::hex << address();

  auto suspend_token = thread->InternalSuspend(true);
  if (!thread->thread_handle().UninstallHWBreakpoint(address())) {
    Warn(FROM_HERE, WarningType::kUninstall, thread->koid(), address());
    return debug::Status("Can't uninstall hardware breakpoint");
  }

  return debug::Status();
}

}  // namespace debug_agent

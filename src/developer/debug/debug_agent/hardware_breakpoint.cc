// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/hardware_breakpoint.h"

#include <inttypes.h>

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

void Warn(WarningType type, zx_koid_t thread_koid, uint64_t address, zx_status_t status) {
  // This happens normally when we receive a ZX_EXCP_THREAD_EXITING exception,
  // making the system ignore our uninstall requests.
  if (status == ZX_ERR_NOT_FOUND)
    return;

  const char* verb = type == WarningType::kInstall ? "install" : "uninstall";
  FXL_LOG(WARNING) << fxl::StringPrintf(
      "Could not %s HW breakpoint for thread %u at "
      "%" PRIX64 ": %s",
      verb, static_cast<uint32_t>(thread_koid), address, zx_status_get_string(status));
}

std::set<zx_koid_t> HWThreadsTargeted(const ProcessBreakpoint& pb) {
  std::set<zx_koid_t> ids;
  bool all_threads = false;
  for (Breakpoint* bp : pb.breakpoints()) {
    // We only care about hardware breakpoints.
    if (bp->type() != debug_ipc::BreakpointType::kHardware)
      continue;

    for (auto& location : bp->settings().locations) {
      // We only install for locations that match this process breakpoint.
      if (location.address != pb.address())
        continue;

      auto thread_id = location.thread_koid;
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
                                       uint64_t address,
                                       std::shared_ptr<arch::ArchProvider> arch_provider)
    : ProcessBreakpoint(breakpoint, process, address), arch_provider_(std::move(arch_provider)) {
  FXL_DCHECK(arch_provider_);
}

HardwareBreakpoint::~HardwareBreakpoint() { Uninstall(); }

bool HardwareBreakpoint::Installed(zx_koid_t thread_koid) const {
  return installed_threads_.count(thread_koid) > 0;
}

// ProcessBreakpoint Implementation ----------------------------------------------------------------

void HardwareBreakpoint::ExecuteStepOver(DebuggedThread* thread) {
  FXL_DCHECK(current_stepping_over_threads_.count(thread->koid()) == 0);
  FXL_DCHECK(!thread->stepping_over_breakpoint());

  DEBUG_LOG(Breakpoint) << LogPreamble(this) << "Thread " << thread->koid() << " is stepping over.";
  thread->set_stepping_over_breakpoint(true);
  current_stepping_over_threads_.insert(thread->koid());

  // HW breakpoints don't need to suspend any threads.
  Uninstall(thread);

  // The thread now can continue with the step over.
  thread->ResumeException();
}

void HardwareBreakpoint::EndStepOver(DebuggedThread* thread) {
  FXL_DCHECK(thread->stepping_over_breakpoint());
  FXL_DCHECK(current_stepping_over_threads_.count(thread->koid()) > 0);

  DEBUG_LOG(Breakpoint) << LogPreamble(this) << "Thread " << thread->koid() << " ending step over.";

  thread->set_stepping_over_breakpoint(false);
  current_stepping_over_threads_.erase(thread->koid());

  // We reinstall this breakpoint for the thread.
  Install(thread);

  // Tell the process we're done stepping over.
  process_->OnBreakpointFinishedSteppingOver();
}

// Update ------------------------------------------------------------------------------------------

zx_status_t HardwareBreakpoint::Update() {
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
      zx_status_t status = Uninstall(thread);
      if (status != ZX_OK)
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

    zx_status_t status = Install(thread);
    if (status != ZX_OK)
      continue;

    installed_threads_.insert(thread_koid);
  }

  return ZX_OK;
}

// Install -----------------------------------------------------------------------------------------

zx_status_t HardwareBreakpoint::Install(DebuggedThread* thread) {
  if (!thread) {
    Warn(WarningType::kInstall, thread->koid(), address(), ZX_ERR_NOT_FOUND);
    return ZX_ERR_NOT_FOUND;
  }

  DEBUG_LOG(Breakpoint) << "Installing HW breakpoint on thread " << thread->koid()
                        << " on address 0x" << std::hex << address();

  auto suspend_token = thread->RefCountedSuspend(true);

  // Do the actual installation.
  zx_status_t status = arch_provider_->InstallHWBreakpoint(thread->handle(), address());
  if (status != ZX_OK) {
    Warn(WarningType::kInstall, thread->koid(), address(), status);
    return status;
  }

  return ZX_OK;
}

// Uninstall ---------------------------------------------------------------------------------------

zx_status_t HardwareBreakpoint::Uninstall() {
  for (zx_koid_t thread_koid : installed_threads_) {
    DebuggedThread* thread = process()->GetThread(thread_koid);
    if (!thread)
      continue;

    zx_status_t res = Uninstall(thread);
    if (res != ZX_OK)
      continue;
  }

  return ZX_OK;
}

zx_status_t HardwareBreakpoint::Uninstall(DebuggedThread* thread) {
  if (!thread) {
    Warn(WarningType::kInstall, thread->koid(), address(), ZX_ERR_NOT_FOUND);
    return ZX_ERR_NOT_FOUND;
  }

  DEBUG_LOG(Breakpoint) << "Removing HW breakpoint on thread " << thread->koid() << " on address 0x"
                        << std::hex << address();

  auto suspend_token = thread->RefCountedSuspend(true);

  zx_status_t status = arch_provider_->UninstallHWBreakpoint(thread->handle(), address());
  if (status != ZX_OK) {
    Warn(WarningType::kInstall, thread->koid(), address(), status);
    return status;
  }

  return ZX_OK;
}

}  // namespace debug_agent

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/watchpoint.h"

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

std::set<zx_koid_t> ThreadsTargeted(const Watchpoint& watchpoint) {
  std::set<zx_koid_t> ids;
  bool all_threads = false;
  for (Breakpoint* bp : watchpoint.breakpoints()) {
    // We only care about hardware breakpoints.
    if (bp->type() != debug_ipc::BreakpointType::kWatchpoint)
      continue;

    for (auto& location : bp->settings().locations) {
      // We only install for locations that match this process breakpoint.
      if (location.address_range != watchpoint.range())
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
    for (DebuggedThread* thread : watchpoint.process()->GetThreads())
      ids.insert(thread->koid());
  }

  return ids;
}

}  // namespace

Watchpoint::Watchpoint(Breakpoint* breakpoint, DebuggedProcess* process,
                       std::shared_ptr<arch::ArchProvider> arch_provider,
                       const debug_ipc::AddressRange& range)
    : ProcessBreakpoint(breakpoint, process, range.begin()),
      range_(range),
      arch_provider_(std::move(arch_provider)) {}

Watchpoint::~Watchpoint() { Uninstall(); }

bool Watchpoint::Installed(zx_koid_t thread_koid) const {
  return installed_threads_.count(thread_koid) > 0;
}

// ProcessBreakpoint Implementation ----------------------------------------------------------------

void Watchpoint::ExecuteStepOver(DebuggedThread* thread) {
  FXL_DCHECK(current_stepping_over_threads_.count(thread->koid()) == 0);
  FXL_DCHECK(!thread->stepping_over_breakpoint());

  DEBUG_LOG(Watchpoint) << LogPreamble(this) << "Thread " << thread->koid() << " is stepping over.";
  thread->set_stepping_over_breakpoint(true);
  current_stepping_over_threads_.insert(thread->koid());

  // HW breakpoints don't need to suspend any threads.
  Uninstall(thread);

  // The thread now can continue with the step over.
  thread->ResumeException();
}

void Watchpoint::EndStepOver(DebuggedThread* thread) {
  FXL_DCHECK(thread->stepping_over_breakpoint());
  FXL_DCHECK(current_stepping_over_threads_.count(thread->koid()) > 0);

  DEBUG_LOG(Watchpoint) << LogPreamble(this) << "Thread " << thread->koid() << " ending step over.";

  thread->set_stepping_over_breakpoint(false);
  current_stepping_over_threads_.erase(thread->koid());

  // We reinstall this breakpoint for the thread.
  Install(thread);

  // Tell the process we're done stepping over.
  process_->OnBreakpointFinishedSteppingOver();
}

// Update ------------------------------------------------------------------------------------------

zx_status_t Watchpoint::Update() {
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
      zx_status_t status = Uninstall(thread);
      if (status != ZX_OK)
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

    zx_status_t status = Install(thread);
    if (status != ZX_OK)
      continue;
  }

  return ZX_OK;
}

// Install -----------------------------------------------------------------------------------------

zx_status_t Watchpoint::Install(DebuggedThread* thread) {
  if (!thread) {
    Warn(WarningType::kInstall, thread->koid(), address(), ZX_ERR_NOT_FOUND);
    return ZX_ERR_NOT_FOUND;
  }

  DEBUG_LOG(Watchpoint) << "Installing watchpoint on thread " << thread->koid() << " on address 0x"
                        << std::hex << address();

  auto suspend_token = thread->RefCountedSuspend(true);

  // Do the actual installation.
  auto result = arch_provider_->InstallWatchpoint(&thread->handle(), range_);
  if (result.status != ZX_OK) {
    Warn(WarningType::kInstall, thread->koid(), address(), result.status);
    return result.status;
  }

  installed_threads_[thread->koid()] = result;

  return ZX_OK;
}

// Uninstall ---------------------------------------------------------------------------------------

zx_status_t Watchpoint::Uninstall() {
  std::vector<zx_koid_t> uninstalled_threads;
  for (auto& [thread_koid, installation] : installed_threads_) {
    DebuggedThread* thread = process()->GetThread(thread_koid);
    if (!thread)
      continue;

    zx_status_t res = Uninstall(thread);
    if (res != ZX_OK)
      continue;

    uninstalled_threads.push_back(thread_koid);
  }

  // Remove them from the list.
  for (zx_koid_t thread_koid : uninstalled_threads) {
    installed_threads_.erase(thread_koid);
  }

  return ZX_OK;
}

zx_status_t Watchpoint::Uninstall(DebuggedThread* thread) {
  if (!thread) {
    Warn(WarningType::kInstall, thread->koid(), address(), ZX_ERR_NOT_FOUND);
    return ZX_ERR_NOT_FOUND;
  }

  DEBUG_LOG(Watchpoint) << "Removing watchpoint on thread " << thread->koid() << " on address 0x"
                        << std::hex << address();

  auto suspend_token = thread->RefCountedSuspend(true);

  zx_status_t status = arch_provider_->UninstallWatchpoint(&thread->handle(), range_);
  if (status != ZX_OK) {
    Warn(WarningType::kInstall, thread->koid(), address(), status);
    return status;
  }

  return ZX_OK;
}

}  // namespace debug_agent

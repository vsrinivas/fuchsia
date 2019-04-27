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

enum class WarningType {
  kInstall,
  kUninstall,
};

void Warn(WarningType type, zx_koid_t thread_koid, uint64_t address,
          zx_status_t status) {
  // This happens normally when we receive a ZX_EXCP_THREAD_EXITING exception,
  // making the system ignore our uninstall requests.
  if (status == ZX_ERR_NOT_FOUND)
    return;

  const char* verb = type == WarningType::kInstall ? "install" : "uninstall";
  FXL_LOG(WARNING) << fxl::StringPrintf(
      "Could not %s HW breakpoint for thread %u at "
      "%" PRIX64 ": %s",
      verb, static_cast<uint32_t>(thread_koid), address,
      zx_status_get_string(status));
}

}  // namespace

HardwareBreakpoint::HardwareBreakpoint(ProcessBreakpoint* process_bp)
    : process_bp_(process_bp) {}

HardwareBreakpoint::~HardwareBreakpoint() { Uninstall(); }

zx_status_t HardwareBreakpoint::Update(
    const std::set<zx_koid_t>& thread_koids) {
  // We get a snapshot of which threads are already installed.
  auto current_threads = installed_threads_;

  // Uninstall pass.
  for (zx_koid_t thread_koid : current_threads) {
    // The ProcessBreakpoint not longer tracks this. Remove.
    if (thread_koids.count(thread_koid) == 0) {
      zx_status_t res = Uninstall(thread_koid);
      if (res != ZX_OK)
        return res;
      installed_threads_.erase(thread_koid);
    }
  }

  // Install pass.
  for (zx_koid_t thread_koid : thread_koids) {
    // If it's already installed, ignore.
    if (installed_threads_.count(thread_koid) > 0)
      continue;

    // If it's new, install.
    if (installed_threads_.count(thread_koid) == 0) {
      zx_status_t res = Install(thread_koid);
      if (res != ZX_OK)
        return res;
      installed_threads_.insert(thread_koid);
    }
  }

  return ZX_OK;
}

zx_status_t HardwareBreakpoint::Install(
    zx_koid_t thread_koid) {
  uint64_t address = process_bp_->address();
  // We need to install this new thread.
  DebuggedThread* thread = process_bp_->process()->GetThread(thread_koid);
  if (!thread) {
    Warn(WarningType::kInstall, thread_koid, address, ZX_ERR_NOT_FOUND);
    return ZX_ERR_NOT_FOUND;
  }

  DEBUG_LOG(Breakpoint) << "Installing HW breakpoint on thread " << thread_koid
                        << " on address 0x" << std::hex << address;

  // Thread needs to be suspended or on an exception (ZX-3772).
  // We make a synchronous (blocking) call.
  auto result = thread->Suspend(true);
  if (result == DebuggedThread::SuspendResult::kError) {
    Warn(WarningType::kInstall, thread_koid, address, ZX_ERR_BAD_STATE);
    return ZX_ERR_BAD_STATE;
  }

  // Do the actual installation.
  auto& arch = arch::ArchProvider::Get();
  zx_status_t status = arch.InstallHWBreakpoint(&thread->thread(), address);
  // TODO: Do we want to remove all other locations when one fails?
  if (status != ZX_OK) {
    Warn(WarningType::kInstall, thread_koid, address, status);
    return status;
  }

  // If the thread was running, we need to resume it.
  if (result == DebuggedThread::SuspendResult::kWasRunning) {
    debug_ipc::ResumeRequest resume;
    resume.how = debug_ipc::ResumeRequest::How::kContinue;
    thread->Resume(resume);
  }

  return ZX_OK;
}

zx_status_t HardwareBreakpoint::Uninstall() {
  for (zx_koid_t thread_koid : installed_threads_) {
    zx_status_t res = Uninstall(thread_koid);
    if (res != ZX_OK)
      return res;
  }

  return ZX_OK;
}

zx_status_t HardwareBreakpoint::Uninstall(
    zx_koid_t thread_koid) {
  uint64_t address = process_bp_->address();
  DebuggedThread* thread = process_bp_->process()->GetThread(thread_koid);
  if (!thread) {
    Warn(WarningType::kInstall, thread_koid, address, ZX_ERR_NOT_FOUND);
    return ZX_ERR_NOT_FOUND;
  }

  DEBUG_LOG(Breakpoint) << "Removing HW breakpoint on thread " << thread_koid
                        << " on address 0x" << std::hex << address;

  // Thread needs to be suspended or on an exception (ZX-3772).
  // We make a synchronous (blocking) call.
  auto result = thread->Suspend(true);
  if (result == DebuggedThread::SuspendResult::kError) {
    Warn(WarningType::kInstall, thread_koid, address, ZX_ERR_BAD_STATE);
    return ZX_ERR_BAD_STATE;
  }

  auto& arch = arch::ArchProvider::Get();
  zx_status_t status = arch.UninstallHWBreakpoint(&thread->thread(), address);
  if (status != ZX_OK) {
    Warn(WarningType::kInstall, thread_koid, address, status);
    return status;
  }

  // If the thread was running, we need to resume it.
  if (result == DebuggedThread::SuspendResult::kWasRunning) {
    debug_ipc::ResumeRequest resume;
    resume.how = debug_ipc::ResumeRequest::How::kContinue;
    thread->Resume(resume);
  }

  return ZX_OK;
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

}  // namespace debug_agent

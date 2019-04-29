// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/debug_agent/process_watchpoint.h"

#include <lib/zx/port.h>
#include <zircon/syscalls/port.h>

#include "src/developer/debug/debug_agent/arch.h"
#include "src/developer/debug/debug_agent/debugged_process.h"
#include "src/developer/debug/debug_agent/debugged_thread.h"
#include "src/developer/debug/debug_agent/watchpoint.h"
#include "src/developer/debug/shared/logging/logging.h"
#include "src/developer/debug/shared/zx_status.h"

namespace debug_agent {

namespace {

std::string KoidsToString(const std::vector<DebuggedThread*>& threads) {
  std::stringstream ss;
  for (DebuggedThread* thread : threads) {
    ss << thread->koid() << ", ";
  }

  return ss.str();
}

}  // namespace

ProcessWatchpoint::ProcessWatchpoint(Watchpoint* watchpoint,
                                     DebuggedProcess* process,
                                     const debug_ipc::AddressRange& range)
    : watchpoint_(watchpoint), process_(process), range_(range) {}

ProcessWatchpoint::~ProcessWatchpoint() { Uninstall(); }

zx_status_t ProcessWatchpoint::Init() { return Update(); }

zx_status_t ProcessWatchpoint::Update() {
  std::set<zx_koid_t> watched_threads = {};
  bool threads_present =
      watchpoint_->ThreadsToInstall(process_->koid(), &watched_threads);
  FXL_DCHECK(threads_present);

  std::vector<DebuggedThread*> threads_to_remove = {};
  std::vector<DebuggedThread*> threads_to_install = {};

  if (watched_threads.empty()) {
    // We need to install this on all the threads, so we simply check for those
    // we're missing.
    for (DebuggedThread* thread : process_->GetThreads()) {
      // See if we already have the thread.
      if (installed_threads_.find(thread->koid()) != installed_threads_.end())
        continue;
      threads_to_install.push_back(thread);
    }
  } else {
    // Here we need to see what threads to remove.
    for (zx_koid_t thread_koid : installed_threads_) {
      // If it is not installed, we leave it as is.
      if (watched_threads.find(thread_koid) != watched_threads.end())
        continue;

      // The thread could have exited.
      DebuggedThread* thread = process_->GetThread(thread_koid);
      if (!thread)
        continue;
      threads_to_remove.push_back(thread);
    }

    // We now check which ones to install.
    for (zx_koid_t thread_koid : watched_threads) {
      // If it's already installed, we leave it as is.
      if (installed_threads_.find(thread_koid) != installed_threads_.end())
        continue;
      // The thread could have exited.
      DebuggedThread* thread = process_->GetThread(thread_koid);
      if (!thread)
        continue;
      threads_to_install.push_back(thread);
    }
  }

  zx_status_t res = UpdateWatchpoints(threads_to_remove, threads_to_install);
  if (res != ZX_OK)
    return res;

  return ZX_OK;
}

debug_ipc::BreakpointStats ProcessWatchpoint::OnHit() {
  FXL_DCHECK(watchpoint_);
  return watchpoint_->OnHit();
}

void ProcessWatchpoint::Uninstall() {
  std::vector<DebuggedThread*> threads_to_remove = {};
  for (zx_koid_t thread_koid : installed_threads_) {
    DebuggedThread* thread = process_->GetThread(thread_koid);
    // Thread might go away at anytime.
    if (!thread)
      continue;
    threads_to_remove.push_back(thread);
  }

  // We only want to remove threads.
  UpdateWatchpoints(threads_to_remove, {});
}

zx_status_t ProcessWatchpoint::UpdateWatchpoints(
    const std::vector<DebuggedThread*>& threads_to_remove,
    const std::vector<DebuggedThread*>& threads_to_install) {
  DEBUG_LOG(Watchpoint) << "Installs: " << KoidsToString(threads_to_install)
                        << "uninstalls: " << KoidsToString(threads_to_remove);

  // We suspend the process synchronously.
  // TODO(donosoc): If this prooves to be too intrusive, we could just stop
  //                the threads that will be changed.
  std::vector<uint64_t> suspended_koids;
  process()->SuspendAll(true, &suspended_koids);

  auto& arch_provider = arch::ArchProvider::Get();
  for (DebuggedThread* thread : threads_to_remove) {
    auto res = arch_provider.UninstallWatchpoint(&thread->thread(), range());
    if (res != ZX_OK)
      return res;
    installed_threads_.erase(thread->koid());
  }

  for (DebuggedThread* thread : threads_to_install) {
    auto res = arch_provider.InstallWatchpoint(&thread->thread(), range());
    if (res != ZX_OK)
      return res;
    installed_threads_.insert(thread->koid());
  }

  // We resume the threads that were affected.
  for (uint64_t thread_koid : suspended_koids) {
    auto* thread = process()->GetThread(thread_koid);
    FXL_DCHECK(thread);
    thread->Resume({});
  }

  return ZX_OK;
}

}  // namespace debug_agent

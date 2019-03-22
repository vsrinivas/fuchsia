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
#include "src/developer/debug/shared/zx_status.h"

namespace debug_agent {

namespace {

zx_status_t InstallWatchpoints(const ProcessWatchpoint& wp,
                               std::vector<DebuggedThread*>* threads) {
  if (threads->empty())
    return ZX_OK;

  // TODO(Cristian): This is wrong! Thread could be running, which means the
  //                 installation will fail. This need to:
  //                 1. Suspend threads.
  //                 2. Install/uninstall watchpoints.
  //                 3. Resume threads that weren't suspended before this.
  //
  // The CL is big enough already so we'll leave it at this.
  auto& arch_provider = arch::ArchProvider::Get();
  for (DebuggedThread* thread : *threads) {
    arch_provider.InstallWatchpoint(&thread->thread(), wp.range());
  }

  return ZX_OK;
}

zx_status_t UninstallWatchpoints(const ProcessWatchpoint& wp,
                                 std::vector<DebuggedThread*>* threads) {
  if (threads->empty())
    return ZX_OK;
  // TODO(Cristian): This is wrong! Thread could be running, which means the
  //                 installation will fail. This need to:
  //                 1. Suspend threads.
  //                 2. Install/uninstall watchpoints.
  //                 3. Resume threads that weren't suspended before this.
  //
  // The CL is big enough already so we'll leave it at this.
  auto& arch_provider = arch::ArchProvider::Get();
  for (DebuggedThread* thread : *threads) {
    arch_provider.UninstallWatchpoint(&thread->thread(), wp.range());
  }

  return ZX_OK;
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

  // NOTE: Each one of this installs/uninstalls will potentially block, because
  //       the thread needs to be stopped in order to mess with the register.
  if (zx_status_t res = UninstallWatchpoints(*this, &threads_to_remove);
      res != ZX_OK) {
    return res;
  }

  if (zx_status_t res = InstallWatchpoints(*this, &threads_to_install);
      res != ZX_OK) {
    return res;
  }

  for (DebuggedThread* thread : threads_to_remove) {
    size_t remove_count = installed_threads_.erase(thread->koid());
    FXL_DCHECK(remove_count == 1u);
  }

  for (DebuggedThread* thread : threads_to_install) {
    auto [it, inserted] = installed_threads_.insert(thread->koid());
    FXL_DCHECK(inserted);
  }

  return ZX_OK;
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

  UninstallWatchpoints(*this, &threads_to_remove);
}

}  // namespace debug_agent

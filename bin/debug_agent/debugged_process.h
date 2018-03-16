// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <zx/process.h>
#include <zx/thread.h>

#include "garnet/lib/debug_ipc/protocol.h"
#include "garnet/public/lib/fxl/macros.h"

class DebuggedProcess {
 public:
  DebuggedProcess(zx_koid_t koid, zx::process proc);
  ~DebuggedProcess();

  zx_koid_t koid() const { return koid_; }
  const zx::process& process() const { return process_; }

  // IPC handlers.
  void OnContinue(const debug_ipc::ContinueRequest& request);

  // Notification that an exception has happened on the given thread. The
  // thread will be in a "suspended on exception" state.
  void OnException(const zx::thread& thread);

 private:
  struct SuspendedThread;

  void ContinueThread(zx_koid_t thread_koid);

  zx_koid_t koid_;
  zx::process process_;

  // Rather than trying to keep a live map of all threads always up-to-date,
  // we only track threads that need to be tracked. Only suspended threads will
  // occur in this map, and once it is resumed it will be deleted.
  std::map<zx_koid_t, SuspendedThread> suspended_threads_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DebuggedProcess);
};

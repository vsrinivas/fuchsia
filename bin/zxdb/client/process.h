// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stdint.h>

#include <map>
#include <memory>

#include "garnet/bin/zxdb/client/client_object.h"
#include "garnet/public/lib/fxl/macros.h"

namespace zxdb {

class Target;
class Thread;

class Process : public ClientObject {
 public:
  // The ID is a monotonically increasing thread index generated based on
  // next_thread_id_.
  using ThreadMap = std::map<size_t, std::unique_ptr<Thread>>;

  // Callback for thread creation and destruction. It will be called with the
  // thread object immediately after its creation and immediately before its
  // destruction.
  enum class ThreadChange {
    kStarted,
    kExiting
  };
  using ThreadChangeCallback = std::function<void(Thread*, ThreadChange)>;

  // The passed-in target owns this process.
  Process(Target* target, uint64_t koid);
  ~Process() override;

  Target* target() { return target_; }
  uint64_t koid() const { return koid_; }

  // Notification from the agent that a thread has started or exited.
  void OnThreadStarting(uint64_t thread_koid);
  void OnThreadExiting(uint64_t thread_koid);

  // Register and unregister for notifications about thread changes. The ID
  // returned by the Start() function can be used to unregister with the Stop()
  // function.
  //
  // These observers are global and will apply to all processes.
  static int StartWatchingGlobalThreadChanges(ThreadChangeCallback callback);
  static void StopWatchingGlobalThreadChanges(int callback_id);

 private:
  Target* target_;
  uint64_t koid_;

  ThreadMap threads_;
  size_t next_thread_id_ = 0;

  static std::map<int, ThreadChangeCallback> thread_change_callbacks_;
  static int next_thread_change_callback_id_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Process);
};

}  // namespace zxdb

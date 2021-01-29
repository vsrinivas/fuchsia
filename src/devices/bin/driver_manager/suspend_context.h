// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_SUSPEND_CONTEXT_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_SUSPEND_CONTEXT_H_

#include "suspend_task.h"

class SuspendContext {
 public:
  enum class Flags : uint32_t {
    kRunning = 0u,
    kSuspend = 1u,
  };

  SuspendContext() = default;

  SuspendContext(Flags flags, uint32_t sflags) : flags_(flags), sflags_(sflags) {}

  ~SuspendContext() = default;

  SuspendContext(SuspendContext&&) = default;
  SuspendContext& operator=(SuspendContext&&) = default;

  void set_task(fbl::RefPtr<SuspendTask> task) { task_ = std::move(task); }

  const SuspendTask* task() const { return task_.get(); }

  Flags flags() const { return flags_; }
  void set_flags(Flags flags) { flags_ = flags; }
  uint32_t sflags() const { return sflags_; }
  async::TaskClosure* watchdog_task() const { return suspend_watchdog_task_.get(); }
  void set_suspend_watchdog_task(std::unique_ptr<async::TaskClosure> watchdog_task) {
    suspend_watchdog_task_ = std::move(watchdog_task);
  }

 private:
  fbl::RefPtr<SuspendTask> task_;
  std::unique_ptr<async::TaskClosure> suspend_watchdog_task_;

  Flags flags_ = Flags::kRunning;

  // suspend flags
  uint32_t sflags_ = 0u;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_SUSPEND_CONTEXT_H_

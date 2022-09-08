// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_V1_SUSPEND_HANDLER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_V1_SUSPEND_HANDLER_H_

#include <fidl/fuchsia.fshost/cpp/wire.h>

#include "src/devices/bin/driver_manager/v1/suspend_matching_task.h"
#include "src/devices/bin/driver_manager/v1/suspend_task.h"

using SuspendCallback = fit::callback<void(zx_status_t)>;

class SuspendHandler {
 public:
  enum class Flags : uint32_t {
    // The system is running, nothing is suspended.
    kRunning = 0u,
    // The entire system is suspended or in the middle of being suspended.
    kSuspend = 1u,
    // The devices whose's drivers live in storage are suspended or in the middle of being
    // suspended.
    kStorageSuspend = 2u,
  };

  // Create a SuspendHandler. `coordinator` is a weak pointer that must outlive `SuspendHandler`.
  SuspendHandler(Coordinator* coordinator, zx::duration suspend_timeout);

  bool InSuspend() const { return flags_ != SuspendHandler::Flags::kRunning; }

  void Suspend(uint32_t flags, SuspendCallback callback);

  // Suspend all of the devices where the device driver lives in storage. This should be called
  // by fshost as it is shutting down.
  void UnregisterSystemStorageForShutdown(SuspendCallback callback);

 private:
  bool AnyTasksInProgress();

  Coordinator* coordinator_;
  zx::duration suspend_timeout_;

  SuspendCallback suspend_callback_;
  fbl::RefPtr<SuspendTask> suspend_task_;

  fbl::RefPtr<SuspendMatchingTask> unregister_system_storage_task_;

  std::unique_ptr<async::TaskClosure> suspend_watchdog_task_;

  Flags flags_ = Flags::kRunning;

  // suspend flags
  uint32_t sflags_ = 0u;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_V1_SUSPEND_HANDLER_H_

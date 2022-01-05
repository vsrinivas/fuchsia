// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_V1_SUSPEND_RESUME_MANAGER_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_V1_SUSPEND_RESUME_MANAGER_H_

#include <fidl/fuchsia.hardware.power.statecontrol/cpp/wire.h>

#include "src/devices/bin/driver_manager/coordinator.h"
#include "src/devices/bin/driver_manager/v1/resume_context.h"
#include "src/devices/bin/driver_manager/v1/resume_task.h"
#include "src/devices/bin/driver_manager/v1/suspend_handler.h"

using ResumeCallback = std::function<void(zx_status_t)>;

class SuspendResumeManager {
 public:
  SuspendResumeManager(Coordinator* coordinator, zx::duration suspend_timeout);

  void Suspend(
      uint32_t flags, SuspendCallback = [](zx_status_t status) {});

  void Resume(
      SystemPowerState target_state, ResumeCallback callback = [](zx_status_t) {});

  bool InSuspend() const;
  bool InResume() const;

  uint32_t GetSuspendFlagsFromSystemPowerState(SystemPowerState state) const;

  SuspendHandler& suspend_handler() { return suspend_handler_; }
  const SuspendHandler& suspend_handler() const { return suspend_handler_; }

 private:
  // Owner.
  Coordinator* coordinator_;

  SuspendHandler suspend_handler_;

  ResumeContext resume_context_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_V1_SUSPEND_RESUME_MANAGER_H_

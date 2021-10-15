// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v1/suspend_matching_task.h"

#include "src/devices/bin/driver_manager/coordinator.h"
#include "src/devices/lib/log/log.h"

SuspendMatchingTask::SuspendMatchingTask(fbl::RefPtr<Device> device, uint32_t flags, Match match,
                                         Completion completion)
    : Task(device->coordinator->dispatcher(), std::move(completion)),
      matches_(std::move(match)),
      device_(std::move(device)),
      flags_(flags) {}

SuspendMatchingTask::~SuspendMatchingTask() = default;

fbl::RefPtr<SuspendMatchingTask> SuspendMatchingTask::Create(fbl::RefPtr<Device> device,
                                                             uint32_t flags, Match match,
                                                             Completion completion) {
  auto task = fbl::MakeRefCounted<SuspendMatchingTask>(std::move(device), flags, std::move(match),
                                                       std::move(completion));
  task->MatchDeviceChildren(*task->device_);
  return task;
}

void SuspendMatchingTask::MatchDeviceChildren(Device& device) {
  for (auto& child : device.children()) {
    if (matches_(child)) {
      if (child.GetActiveSuspend() == nullptr) {
        AddDependency(child.RequestSuspendTask(flags_));
        continue;
      }
    } else {
      MatchDeviceChildren(child);
    }
  }
}

void SuspendMatchingTask::Run() { Complete(ZX_OK); }

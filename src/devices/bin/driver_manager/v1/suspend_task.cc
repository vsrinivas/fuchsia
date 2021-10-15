// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v1/suspend_task.h"

#include "src/devices/bin/driver_manager/coordinator.h"

namespace {

bool IsDeviceBeingRemoved(fbl::RefPtr<Device> device) {
  auto remove_task = device->GetActiveRemove();
  if ((device->state() == Device::State::kUnbinding) || (device->state() == Device::State::kDead) ||
      (remove_task != nullptr)) {
    return true;
  }
  return false;
}

}  // namespace

SuspendTask::SuspendTask(fbl::RefPtr<Device> device, uint32_t flags, Completion completion)
    : Task(device->coordinator->dispatcher(), std::move(completion)),
      device_(std::move(device)),
      flags_(flags) {}

SuspendTask::~SuspendTask() = default;

fbl::RefPtr<SuspendTask> SuspendTask::Create(fbl::RefPtr<Device> device, uint32_t flags,
                                             Completion completion) {
  return fbl::MakeRefCounted<SuspendTask>(std::move(device), flags, std::move(completion));
}

void SuspendTask::Run() {
  bool found_more_dependencies = false;
  for (auto& child : device_->children()) {
    // Use a switch statement here so that this gets reconsidered if we add
    // more states.
    switch (child.state()) {
      // If the device is dead, any existing suspend task would have been forcibly completed.
      case Device::State::kDead:
      case Device::State::kUnbinding:
      case Device::State::kSuspended:
        continue;
      case Device::State::kInitializing:
      case Device::State::kSuspending:
      case Device::State::kActive:
      case Device::State::kResuming:
      case Device::State::kResumed:
        break;
    }
    if (!IsDeviceBeingRemoved(fbl::RefPtr(&child))) {
      AddDependency(child.RequestSuspendTask(flags_));
      found_more_dependencies = true;
    }
  }
  if (found_more_dependencies) {
    return;
  }

  // Handle the device proxy, if it exists, after children since they might
  // depend on it.
  if (device_->proxy() != nullptr) {
    switch (device_->proxy()->state()) {
      case Device::State::kDead:
      case Device::State::kSuspended:
      case Device::State::kResuming:
      case Device::State::kResumed:
        break;
      case Device::State::kInitializing:
      case Device::State::kUnbinding:
      case Device::State::kSuspending:
      case Device::State::kActive: {
        AddDependency(device_->proxy()->RequestSuspendTask(flags_));
        return;
      }
    }
  }

  if (device_->state() == Device::State::kInitializing) {
    auto init_task = device_->GetActiveInit();
    ZX_ASSERT(init_task != nullptr);
    AddDependency(init_task);
    return;
  }

  // The device is about to be removed, complete suspend right away
  if (IsDeviceBeingRemoved(device_)) {
    return Complete(ZX_OK);
  }

  // The device is about to be resumed, wait for it to complete.
  if (device_->state() == Device::State::kResuming) {
    auto resume_task = device_->GetActiveResume();
    AddDependency(resume_task);
    return;
  }

  // Check if this device is not in a driver_host.  This happens for the
  // top-level devices like /sys provided by devcoordinator,
  // or the device is already dead.
  if (device_->host() == nullptr) {
    // device shouldn't be set to suspended if it's already dead
    if (device_->state() != Device::State::kDead) {
      device_->set_state(Device::State::kSuspended);
    }
    return Complete(ZX_OK);
  }

  auto completion = ExtendLifetimeWith([this](zx_status_t status) {
    Complete(status);
    device_->DropSuspendTask();
  });
  device_->SendSuspend(flags_, std::move(completion));
}

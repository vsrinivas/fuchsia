// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "unbind-task.h"

#include "../shared/log.h"
#include "coordinator.h"

namespace devmgr {

UnbindTask::UnbindTask(fbl::RefPtr<Device> device, bool do_unbind, Completion completion)
    : Task(device->coordinator->dispatcher(), std::move(completion)),
      device_(std::move(device)),
      device_parent_(device_->parent()),
      do_unbind_(do_unbind) {}

UnbindTask::~UnbindTask() = default;

fbl::RefPtr<UnbindTask> UnbindTask::Create(fbl::RefPtr<Device> device, bool do_unbind,
                                           Completion completion) {
  return fbl::MakeRefCounted<UnbindTask>(std::move(device), do_unbind, std::move(completion));
}

// Schedules the unbind tasks for the device's children.
void UnbindTask::ScheduleUnbindChildren() {
  // If we have a proxy, that counts as our child.
  // The proxy unbind task will schedule unbinding of our actual children list.
  if (device_->proxy() != nullptr) {
    switch (device_->proxy()->state()) {
      case Device::State::kDead:
      // We are already in the process of unbinding ourselves and our children,
      // no need to create a new one.
      case Device::State::kUnbinding:
        return;
      case Device::State::kSuspended:
      // The created unbind task will wait for the suspend to complete.
      case Device::State::kSuspending:
      case Device::State::kActive: {
        device_->proxy()->RequestUnbindTask(false /* do_unbind */);
        return;
      }
    }
  }

  auto children = device_->children();
  // Check if we are a proxy device, in which case our "children" are actually stored
  // in our parent's children list.
  // We should use the stored parent reference as the device's parent reference
  // would have been cleared.
  if ((device_->flags & DEV_CTX_PROXY) && (device_parent_ != nullptr)) {
    children = device_parent_->children();
  }

  for (auto& child : children) {
    // Use a switch statement here so that this gets reconsidered if we add
    // more states.
    switch (child.state()) {
      case Device::State::kDead:
      case Device::State::kUnbinding:
        continue;
      case Device::State::kSuspended:
      case Device::State::kSuspending:
      case Device::State::kActive:
        break;
    }
    child.RequestUnbindTask();
  }
}

void UnbindTask::Run() {
  // The device is currently suspending, wait for it to complete.
  if (device_->state() == Device::State::kSuspending) {
    auto suspend_task = device_->GetActiveSuspend();
    ZX_ASSERT(suspend_task != nullptr);
    AddDependency(suspend_task);
    return;
  }

  auto completion = [this](zx_status_t status) {
    Complete(status);
    // We schedule the unbinding of children after the task completes.
    // This is as new remove-only tasks may be created as a result of a device's unbind hook.
    switch (status) {
      case ZX_OK:
        ScheduleUnbindChildren();
        return;
      case ZX_ERR_UNAVAILABLE:
        // Task was already scheduled.
        return;
      default:
        log(ERROR, "unbind task: %s failed, err %d\n", device_->name().data(), status);
        // Forcibly remove all devices from the devhost.
        // This will also handle scheduling a new unbind task for any proxies.
        device_->coordinator->RemoveDevice(device_, true /* forced */);
        return;
    }
  };

  // Check if this device is not in a devhost.  This happens for the
  // top-level devices like /sys provided by devcoordinator, or if the device
  // has already been removed.
  if (device_->host() == nullptr) {
    return completion(ZX_OK);
  }

  zx_status_t status = ZX_OK;
  if (do_unbind_) {
    status = device_->SendUnbind(std::move(completion));
  } else {
    status = device_->SendCompleteRemoval(std::move(completion));
  }
  if (status != ZX_OK) {
    return completion(status);
  }
}

} // namespace devmgr


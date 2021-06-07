// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "unbind_task.h"

#include <zircon/status.h>

#include "coordinator.h"
#include "src/devices/lib/log/log.h"

UnbindTask::UnbindTask(fbl::RefPtr<Device> device, UnbindTaskOpts opts, Completion completion)
    : Task(device->coordinator->dispatcher(), std::move(completion), opts.post_on_create),
      device_(std::move(device)),
      do_unbind_(opts.do_unbind),
      driver_host_requested_(opts.driver_host_requested) {}

UnbindTask::~UnbindTask() = default;

fbl::RefPtr<UnbindTask> UnbindTask::Create(fbl::RefPtr<Device> device, UnbindTaskOpts opts,
                                           Completion completion) {
  return fbl::MakeRefCounted<UnbindTask>(std::move(device), opts, std::move(completion));
}

// Schedules the unbind tasks for the device's children.
void UnbindTask::ScheduleUnbindChildren() {
  auto remove_task = device_->GetActiveRemove();
  if (remove_task == nullptr) {
    LOGF(ERROR, "Unbind task failed, but no remove task exists for device %p '%s'", device_.get(),
         device_->name().data());
    return;
  }

  // Remove task needs to wait for the current unbind task to complete.
  remove_task->AddDependency(fbl::RefPtr(this));

  fbl::RefPtr<UnbindTask> proxy_unbind_task = nullptr;
  if (device_->proxy() != nullptr) {
    switch (device_->proxy()->state()) {
      case Device::State::kDead:
      // We are already in the process of unbinding ourselves and our children,
      // no need to create a new one.
      case Device::State::kUnbinding:
        break;
      // The created unbind task will wait for the init to complete.
      case Device::State::kInitializing:
      case Device::State::kSuspended:
      // The created unbind task will wait for the suspend to complete.
      case Device::State::kSuspending:
      case Device::State::kResuming:
      case Device::State::kResumed:
      case Device::State::kActive: {
        device_->proxy()->CreateUnbindRemoveTasks(UnbindTaskOpts{
            .do_unbind = false, .post_on_create = false, .driver_host_requested = false});

        proxy_unbind_task = device_->proxy()->GetActiveUnbind();
        // The proxy's unbind task may have already completed, in which case we only
        // have to wait on the remove task.
        if (proxy_unbind_task) {
          proxy_unbind_task->AddDependency(fbl::RefPtr(this));
        }
        // The device should not be removed until its children have been removed.
        remove_task->AddDependency(device_->proxy()->GetActiveRemove());
      }
    }
    // A device may have both a proxy device and children devices,
    // so continue rather than returning early.
  }

  auto children = device_->children();
  // Though we try to schedule the unbind tasks for both a device's proxy and its children,
  // its possible for ScheduleRemove() to be called directly on a proxy unbind task, such as in the
  // case of a forced remove.
  // To handle this, we need to schedule unbind tasks for the proxy "children", which are actually
  // stored in our parent's children list.
  // This means we may end up adding the children as dependent on a proxy device twice,
  // but that is handled by the task logic.
  if (device_->flags & DEV_CTX_PROXY && device_->parent()) {
    children = device_->parent()->children();
  }

  for (auto& child : children) {
    // Use a switch statement here so that this gets reconsidered if we add
    // more states.
    switch (child.state()) {
      case Device::State::kDead:
      case Device::State::kUnbinding:
        continue;
      case Device::State::kInitializing:
      case Device::State::kSuspended:
      case Device::State::kSuspending:
      case Device::State::kResuming:
      case Device::State::kResumed:
      case Device::State::kActive:
        break;
    }
    child.CreateUnbindRemoveTasks(
        UnbindTaskOpts{.do_unbind = true, .post_on_create = false, .driver_host_requested = false});

    auto parent = device_->proxy() != nullptr ? device_->proxy() : device_;

    // The child unbind task may have already completed, in which case we only need to wait
    // for the child's remove task.
    auto child_unbind_task = child.GetActiveUnbind();
    if (child_unbind_task) {
      auto parent_unbind_task = parent->GetActiveUnbind();
      if (parent_unbind_task) {
        child_unbind_task->AddDependency(parent_unbind_task);
      }
    }
    // Since the child is not dead, the remove task must exist.
    auto child_remove_task = child.GetActiveRemove();
    ZX_ASSERT(child_remove_task != nullptr);
    auto parent_remove_task = parent->GetActiveRemove();
    if (parent_remove_task) {
      parent_remove_task->AddDependency(child_remove_task);
    }
  }
}

void UnbindTask::Run() {
  LOGF(INFO, "Running unbind task for device %p '%s', do_unbind %d", device_.get(),
       device_->name().data(), do_unbind_);

  if (device_->state() == Device::State::kInitializing) {
    auto init_task = device_->GetActiveInit();
    ZX_ASSERT(init_task != nullptr);
    AddDependency(init_task);
    return;
  }

  // The device is currently suspending, wait for it to complete.
  if (device_->state() == Device::State::kSuspending) {
    auto suspend_task = device_->GetActiveSuspend();
    ZX_ASSERT(suspend_task != nullptr);
    AddDependency(suspend_task);
    return;
  }

  if (device_->state() == Device::State::kResuming) {
    auto resume_task = device_->GetActiveResume();
    ZX_ASSERT(resume_task != nullptr);
    AddDependency(resume_task);
    return;
  }

  // We need to schedule the child tasks before completing the unbind task runs,
  // as composite device disassociation may occur.
  ScheduleUnbindChildren();

  auto completion = [this](zx_status_t status) {
    // If this unbind task failed, force remove all devices from the driver_host.
    bool failed_unbind = status != ZX_OK && status != ZX_ERR_UNAVAILABLE;
    if (failed_unbind && device_->state() != Device::State::kDead) {
      LOGF(ERROR, "Unbind task failed, force removing device %p '%s': %s", device_.get(),
           device_->name().data(), zx_status_get_string(status));
      device_->coordinator->RemoveDevice(device_, true /* forced */);
    }
    // The forced removal will schedule new unbind tasks if needed (e.g. for proxy tasks),
    // so we should not propagate errors other than ZX_ERR_UNAVAILABLE.
    Complete(status == ZX_OK ? ZX_OK : ZX_ERR_UNAVAILABLE);
  };

  // Check if we should send the unbind request to the driver_host. We do not want to send it if:
  //  - This device is not in a driver_host.  This happens for the top-level devices like /sys
  //    provided by devcoordinator, or if the device has already been removed.
  //  - device_remove does not call unbind on the device.
  bool send_unbind = (device_->host() != nullptr) && do_unbind_;
  zx_status_t status = ZX_OK;
  if (send_unbind) {
    status = device_->SendUnbind(std::move(completion));
    if (status == ZX_OK) {
      // Sent the unbind request, the driver_host will call our completion when ready.
      return;
    }
  }
  // Save a copy of the device in case this task's destructor runs after the
  // completion returns.
  fbl::RefPtr<Device> device = device_;
  // No unbind request sent, need to call the completion now.
  completion(status);
  // Since the device didn't successfully send an Unbind request, it will not
  // drop our unbind task reference. We need to drop it now unless the error was
  // that the unbind request had already been sent (ZX_ERR_UNAVAILABLE).
  if (status != ZX_ERR_UNAVAILABLE) {
    device->DropUnbindTask();
  }
}

RemoveTask::RemoveTask(fbl::RefPtr<Device> device, Completion completion)
    : Task(device->coordinator->dispatcher(), std::move(completion), false /* post_on_create */),
      device_(std::move(device)) {}

RemoveTask::~RemoveTask() = default;

fbl::RefPtr<RemoveTask> RemoveTask::Create(fbl::RefPtr<Device> device, Completion completion) {
  return fbl::MakeRefCounted<RemoveTask>(std::move(device), std::move(completion));
}

void RemoveTask::Run() {
  LOGF(INFO, "Running remove task for device %p '%s'", device_.get(), device_->name().data());
  auto completion = [this](zx_status_t status) {
    // If this remove task failed, force remove all devices from the driver_host.
    bool failed_remove = status != ZX_OK && status != ZX_ERR_UNAVAILABLE;
    if (failed_remove && device_->state() != Device::State::kDead) {
      LOGF(ERROR, "Remove task failed, forcing remove of device %p '%s': %s", device_.get(),
           device_->name().data(), zx_status_get_string(status));
      device_->coordinator->RemoveDevice(device_, true /* forced */);
    }
    // The forced removal will schedule new remove tasks if needed (e.g. for proxy tasks),
    // so we should not propagate errors other than ZX_ERR_UNAVAILABLE.
    Complete(status == ZX_OK ? ZX_OK : ZX_ERR_UNAVAILABLE);
  };

  zx_status_t status = ZX_OK;
  if (device_->host() != nullptr) {
    status = device_->SendCompleteRemoval(std::move(completion));
    if (status == ZX_OK) {
      // Sent the remove request, the driver_host will call our completion when ready.
      return;
    }
  }
  // Save a copy of the device in case this task's destructor runs after the
  // completion returns.
  fbl::RefPtr<Device> device = device_;
  // No remove request sent, need to call the completion now.
  completion(status);
  // Since the device didn't successfully send an CompleteRemoval request, it will not
  // drop our remove task reference. We need to drop it now unless the error was
  // that the remove request had already been sent (ZX_ERR_UNAVAILABLE).
  if (status != ZX_ERR_UNAVAILABLE) {
    device->DropRemoveTask();
  }
}

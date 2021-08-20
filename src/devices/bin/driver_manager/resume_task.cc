// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "resume_task.h"

#include <zircon/errors.h>

#include "coordinator.h"

ResumeTask::ResumeTask(fbl::RefPtr<Device> device, uint32_t target_system_state,
                       Completion completion)
    : Task(device->coordinator->dispatcher(), std::move(completion), true),
      device_(std::move(device)),
      target_system_state_(target_system_state) {}

ResumeTask::~ResumeTask(){};

fbl::RefPtr<ResumeTask> ResumeTask::Create(fbl::RefPtr<Device> device, uint32_t target_system_state,
                                           Completion completion) {
  return fbl::MakeRefCounted<ResumeTask>(std::move(device), target_system_state,
                                         std::move(completion));
}

bool ResumeTask::AddParentResumeTask() {
  if (device_->parent() == nullptr) {
    // For a composite device, each fragment is a parent.
    // Until all the fragments resume, composite device cannot
    // be resumed.
    if (device_->composite()) {
      bool parent_dependency_added = false;
      for (auto& fragment : device_->composite()->bound_fragments()) {
        auto dev = fragment.bound_device();
        if (dev != nullptr) {
          switch (dev->state()) {
            case Device::State::kDead:
              // One of the parents is dead, we cant resume the device.
              // Complete this task.
              Complete(ZX_ERR_NOT_CONNECTED);
              return false;
            case Device::State::kActive:
            case Device::State::kInitializing:
              continue;
            case Device::State::kUnbinding:
            case Device::State::kSuspending:
            case Device::State::kResuming:
            case Device::State::kResumed:
            case Device::State::kSuspended:
              parent_dependency_added = true;
              AddDependency(dev->RequestResumeTask(target_system_state_));
          }
        }
      }
      if (parent_dependency_added) {
        return true;
      }
    }
    return false;
  }

  switch (device_->parent()->state()) {
    case Device::State::kDead:
      // If parent is dead, we cant resume the device.
      // Complete this task.
      Complete(ZX_ERR_NOT_CONNECTED);
      return false;
    case Device::State::kInitializing:
    case Device::State::kActive:
      return false;
    case Device::State::kUnbinding:
    case Device::State::kSuspending:
    case Device::State::kResuming:
    case Device::State::kResumed:
    case Device::State::kSuspended:
      AddDependency(device_->parent()->RequestResumeTask(target_system_state_));
      return true;
  }
  return false;
}

bool ResumeTask::AddProxyResumeTask() {
  if (device_->flags & DEV_CTX_PROXY) {
    return false;
  }
  if (device_->parent() == nullptr) {
    return false;
  }
  if (device_->parent()->proxy() == nullptr) {
    return false;
  }
  switch (device_->parent()->proxy()->state()) {
    case Device::State::kDead:
      // If parent is dead, we cant resume the device.
      // Complete this task.
      Complete(ZX_ERR_NOT_CONNECTED);
      return false;
    case Device::State::kInitializing:
    case Device::State::kActive:
      return false;
    case Device::State::kUnbinding:
    case Device::State::kSuspending:
    case Device::State::kResuming:
    case Device::State::kResumed:
    case Device::State::kSuspended:
      AddDependency(device_->parent()->proxy()->RequestResumeTask(target_system_state_));
      return true;
  }
  return false;
}

void ResumeTask::Run() {
  switch (device_->state()) {
    case Device::State::kDead:
      return Complete(ZX_ERR_NOT_CONNECTED);
    case Device::State::kActive:
      return Complete(ZX_OK);
    case Device::State::kInitializing: {
      // Currently resume tasks are not scheduled during suspend.
      // Since a device cannot be suspended until init has completed,
      // it does not make sense for a resume task to be running during init.
      ZX_ASSERT(device_->state() != Device::State::kInitializing);
      return;
    }
    case Device::State::kSuspending:
    case Device::State::kUnbinding:
    case Device::State::kSuspended:
    case Device::State::kResumed:
    case Device::State::kResuming:
      break;
  }

  // The device is about to be unbound, wait for it to complete.
  // Eventually we complete when device goes to DEAD
  if (device_->state() == Device::State::kUnbinding) {
    // The remove task depends on the unbind task, so wait for that to complete.
    auto remove_task = device_->GetActiveRemove();
    ZX_ASSERT(remove_task != nullptr);
    AddDependency(remove_task);
    return;
  }

  // The device is about to be suspended, wait for it to complete.
  if (device_->state() == Device::State::kSuspending) {
    auto suspend_task = device_->GetActiveSuspend();
    ZX_ASSERT(suspend_task != nullptr);
    AddDependency(suspend_task);
    return;
  }

  // Handle the device proxy, if it exists, before the parent
  if (AddProxyResumeTask()) {
    return;
  }

  // Add a dependent resume task on parent.
  if (AddParentResumeTask()) {
    return;
  }
  // Check if this device is not in a driver_host.  This happens for the
  // top-level devices like /sys provided by devcoordinator,
  // or the device is already dead.
  if (device_->host() == nullptr) {
    // pretend this completed successfully.
    device_->set_state(Device::State::kActive);
    return Complete(ZX_OK);
  }

  auto completion = [this](zx_status_t status) {
    if (status == ZX_OK) {
      device_->set_state(Device::State::kActive);
    } else {
      device_->set_state(Device::State::kSuspended);
    }
    Complete(status);
    return;
  };

  device_->SendResume(target_system_state_, std::move(completion));
}

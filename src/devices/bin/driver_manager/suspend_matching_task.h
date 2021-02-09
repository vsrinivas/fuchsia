// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_SUSPEND_MATCHING_TASK_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_SUSPEND_MATCHING_TASK_H_

#include "device.h"
#include "task.h"

// This task walks a given `device` and its children and will suspend any devices
// that match the given function. Remember that suspending a device will also suspend
// all of that device's children.
class SuspendMatchingTask final : public Task {
 public:
  using Match = fit::function<bool(const Device& device)>;
  static fbl::RefPtr<SuspendMatchingTask> Create(fbl::RefPtr<Device> root, uint32_t flags,
                                                 Match match, Completion completion = nullptr);

  // Don/t invoke this, use Create
  SuspendMatchingTask(fbl::RefPtr<Device> device, uint32_t flags, Match match,
                      Completion completion);

  uint32_t suspend_flags() const { return flags_; }

  ~SuspendMatchingTask() final;

  const Device& device() const { return *device_; }

  fbl::String TaskDescription() const final {
    return fbl::String::Concat({"suspendmatching(", device_->name(), ")"});
  }

 private:
  void Run() final;

  void MatchDeviceChildren(Device& device);

  Match matches_;
  // The device being suspended
  fbl::RefPtr<Device> device_;
  // The target suspend flags
  uint32_t flags_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_SUSPEND_MATCHING_TASK_H_

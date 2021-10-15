// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVICES_BIN_DRIVER_MANAGER_INIT_TASK_H_
#define SRC_DEVICES_BIN_DRIVER_MANAGER_INIT_TASK_H_

#include "src/devices/bin/driver_manager/device.h"
#include "src/devices/bin/driver_manager/v1/task.h"

// This is used for sending |Init| requests.
class InitTask final : public Task {
 public:
  static fbl::RefPtr<InitTask> Create(fbl::RefPtr<Device> device, Completion completion = nullptr);

  // Don't invoke this, use Create
  InitTask(fbl::RefPtr<Device> device, Completion completion);

  ~InitTask() final;

  fbl::String TaskDescription() const final {
    return fbl::String::Concat({"init(", device_->name(), ")"});
  }

 private:
  void Run() final;

  fbl::RefPtr<Device> device_;
};

#endif  // SRC_DEVICES_BIN_DRIVER_MANAGER_INIT_TASK_H_

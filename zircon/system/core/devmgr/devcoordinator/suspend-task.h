// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_DEVMGR_DEVCOORDINATOR_SUSPEND_TASK_H_
#define ZIRCON_SYSTEM_CORE_DEVMGR_DEVCOORDINATOR_SUSPEND_TASK_H_

#include "device.h"
#include "task.h"

namespace devmgr {

class SuspendTask final : public Task {
 public:
  static fbl::RefPtr<SuspendTask> Create(fbl::RefPtr<Device> device, uint32_t flags,
                                         Completion completion = nullptr);

  // Don/t invoke this, use Create
  SuspendTask(fbl::RefPtr<Device> device, uint32_t flags, Completion completion);

  uint32_t suspend_flags() { return flags_; }

  ~SuspendTask() final;

 private:
  void Run() final;

  // The device being suspended
  fbl::RefPtr<Device> device_;
  // The target suspend flags
  uint32_t flags_;
};

}  // namespace devmgr

#endif  // ZIRCON_SYSTEM_CORE_DEVMGR_DEVCOORDINATOR_SUSPEND_TASK_H_

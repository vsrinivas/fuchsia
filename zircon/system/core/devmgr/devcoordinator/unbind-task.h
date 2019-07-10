// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_DEVMGR_DEVCOORDINATOR_UNBIND_TASK_H_
#define ZIRCON_SYSTEM_CORE_DEVMGR_DEVCOORDINATOR_UNBIND_TASK_H_

#include "device.h"
#include "task.h"

namespace devmgr {

// This is used for sending both |CompleteRemoval| and |Unbind| requests.
// For compatibility with the current device lifecycle model, unbind is not invoked
// on the device that |ScheduleRemove| was called on.
class UnbindTask final : public Task {
 public:
  static fbl::RefPtr<UnbindTask> Create(fbl::RefPtr<Device> device, bool do_unbind = true,
                                        Completion completion = nullptr);

  // Don't invoke this, use Create
  UnbindTask(fbl::RefPtr<Device> device, bool do_unbind, Completion completion);

  ~UnbindTask() final;
 private:
  void ScheduleUnbindChildren();
  void Run() final;

  // The device being removed or unbound.
  fbl::RefPtr<Device> device_;
  // The immediate parent of |device_|. This is required as the device's parent reference
  // will be cleared by the time the unbind task completes, and we need it for scheduling
  // the unbind tasks for the children of proxies.
  fbl::RefPtr<Device> device_parent_;
  // If true, |Unbind| will be sent to the devhost, otherwise |RemoveDevice|.
  bool do_unbind_;
};

} // namespace devmgr

#endif  // ZIRCON_SYSTEM_CORE_DEVMGR_DEVCOORDINATOR_UNBIND_TASK_H_

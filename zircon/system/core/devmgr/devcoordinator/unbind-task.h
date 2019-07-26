// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_CORE_DEVMGR_DEVCOORDINATOR_UNBIND_TASK_H_
#define ZIRCON_SYSTEM_CORE_DEVMGR_DEVCOORDINATOR_UNBIND_TASK_H_

#include "device.h"
#include "task.h"

namespace devmgr {

// This is not nested so we can forward declare it in device.h.
struct UnbindTaskOpts {
  // Whether to call the unbind hook.
  bool do_unbind;
  // Whether to immediately post this task to the async dispatcher.
  bool post_on_create;
  // Whether the devhost (i.e. not the devcoordinator) called |ScheduleRemove| on the device.
  bool devhost_requested;
};

// This is used for sending both |CompleteRemoval| and |Unbind| requests.
// For compatibility with the current device lifecycle model, unbind is not invoked
// on the device that |ScheduleRemove| was called on.
class UnbindTask final : public Task {
 public:
  static fbl::RefPtr<UnbindTask> Create(fbl::RefPtr<Device> device, UnbindTaskOpts opts,
                                        Completion completion = nullptr);

  // Don't invoke this, use Create
  UnbindTask(fbl::RefPtr<Device> device, UnbindTaskOpts opts, Completion completion);

  ~UnbindTask() final;

  void set_do_unbind(bool do_unbind) { do_unbind_ = do_unbind; }

  bool devhost_requested() const { return devhost_requested_; }

 private:
  void ScheduleUnbindChildren();
  void Run() final;

  // The device being removed or unbound.
  fbl::RefPtr<Device> device_;
  // If true, |Unbind| will be sent to the devhost, otherwise |CompleteRemoval|.
  bool do_unbind_;
  // True if this task is for the device that had |ScheduleRemove| called on it by a devhost,
  // false otherwise.
  bool devhost_requested_;
};

}  // namespace devmgr

#endif  // ZIRCON_SYSTEM_CORE_DEVMGR_DEVCOORDINATOR_UNBIND_TASK_H_

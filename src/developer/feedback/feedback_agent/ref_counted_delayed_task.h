// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_REF_COUNTED_DELAYED_TASK_H_
#define SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_REF_COUNTED_DELAYED_TASK_H_

#include <lib/async/dispatcher.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <functional>

#include "src/lib/fxl/functional/cancelable_callback.h"
#include "src/lib/fxl/macros.h"

namespace feedback {

// Represents a delayed task that will only execute once nobody holds a reference to it. If somebody
// acquires a reference after scheduling, but before execution, the task is canceled and
// only-rescheduled once all references are released.
class RefCountedDelayedTask {
 public:
  RefCountedDelayedTask(async_dispatcher_t* dispatcher, std::function<void()> task,
                        zx::duration delay);

  // Acquires a reference to the task, canceling any current scheduling.
  void Acquire();

  // Release a reference to the task. If the number of references reaches 0, the task is scheduled.
  //
  // Before releasing a reference to the task, we check the number of references. If it is:
  // * 0, return ZX_ERR_BAD_STATE
  // * > 1, return ZX_OK
  // * 1, return the result of scheduling the task. If scheduling fails, the reference count is left
  //   at 1.
  zx_status_t Release();

 private:
  async_dispatcher_t* dispatcher_;
  std::function<void()> task_;
  zx::duration delay_;

  uint64_t ref_count_ = 0;

  fxl::CancelableClosure cancelable_task_;

  FXL_DISALLOW_COPY_AND_ASSIGN(RefCountedDelayedTask);
};

}  // namespace feedback

#endif  // SRC_DEVELOPER_FEEDBACK_FEEDBACK_AGENT_REF_COUNTED_DELAYED_TASK_H_

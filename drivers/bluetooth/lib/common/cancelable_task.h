// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <async/task.h>

#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/time/time_delta.h"

namespace btlib {
namespace common {

// Wrapper around async::Task that maintains the state of the task. This class
// is not thread safe and must only be used on one thread.
//
// Requires an initialized fsl::MessageLoop on the creation thread.
class CancelableTask final {
 public:
  CancelableTask();
  ~CancelableTask();

  // Returns true if the task has been posted. A posted task cannot be re-posted
  // until the task runs or gets canceled.
  bool posted() const { return posted_; }

  // Cancels a previously posted task. Does nothing if no task was posted.
  void Cancel();

  // Posts a task to be run after |nanoseconds|. Returns false is a task is
  // currently posted or if the operation fails.
  bool Post(fxl::Closure task, zx_duration_t nanoseconds);

 private:
  bool posted_;
  async::Task task_;

  FXL_DISALLOW_COPY_AND_ASSIGN(CancelableTask);
};

}  // namespace common
}  // namespace btlib

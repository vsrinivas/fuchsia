// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_RUN_ONCE_H_
#define SRC_SYS_FUZZING_COMMON_RUN_ONCE_H_

#include <lib/fit/function.h>
#include <lib/sync/completion.h>

#include <atomic>

#include "src/lib/fxl/macros.h"

namespace fuzzing {

// This class is a wrapper that ensures closures are idempotent, even if they are called multiple
// times and/or concurrently.
class RunOnce final {
 public:
  explicit RunOnce(fit::closure task);
  ~RunOnce();

  // Runs the task, or waits until the task is complete if another call to |Run| has been made
  // previously.
  void Run();

 private:
  fit::closure task_;
  std::atomic<bool> running_ = false;
  sync_completion_t ran_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(RunOnce);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_RUN_ONCE_H_

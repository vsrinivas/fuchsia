// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_TEST_FAKE_TASK_RUNNER_H_
#define LIB_FXL_TEST_FAKE_TASK_RUNNER_H_

#include <queue>

#include "lib/fxl/tasks/task_runner.h"

namespace fxl {

// TaskRunner that stores tasks in a queue, to be run when requested.
class FakeTaskRunner : public TaskRunner {
 public:
  void PostTask(Closure task) override;
  void PostTaskForTime(Closure task, TimePoint target_time) override;
  void PostDelayedTask(Closure task, TimeDelta delay) override;
  bool RunsTasksOnCurrentThread() override;

  // Run the tasks in the queue until it's empty or until QuitNow() is called.
  void Run();

  // Immediately stops iteration in Run().
  void QuitNow();

 private:
  FakeTaskRunner();
  ~FakeTaskRunner() override;

  std::queue<Closure> task_queue_;
  bool should_quit_ = false;
  bool running_ = false;

  FRIEND_MAKE_REF_COUNTED(FakeTaskRunner);
};

}  // namespace fxl

#endif  // LIB_FXL_TEST_FAKE_TASK_RUNNER_H_

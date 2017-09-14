// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <thread>
#include <vector>

#include <zx/port.h>

#include "lib/fxl/macros.h"
#include "lib/fxl/tasks/task_runner.h"

namespace media {

// Runs tasks on multiple processes.
class MultiprocTaskRunner : public fxl::TaskRunner {
 public:
  MultiprocTaskRunner(uint32_t thread_count);

  ~MultiprocTaskRunner();

  // TaskRunner implementation.
  void PostTask(fxl::Closure task) override;

  void PostTaskForTime(fxl::Closure task, fxl::TimePoint target_time) override;

  void PostDelayedTask(fxl::Closure task, fxl::TimeDelta delay) override;

  bool RunsTasksOnCurrentThread() override;

 private:
  void Worker(uint32_t thread_number);

  void QueuePacket(uint64_t key, void* payload = nullptr);

  zx::port port_;
  std::vector<std::thread> threads_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MultiprocTaskRunner);
};

}  // namespace media

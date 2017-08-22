// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <thread>
#include <vector>

#include <mx/port.h>

#include "lib/ftl/macros.h"
#include "lib/ftl/tasks/task_runner.h"

namespace media {

// Runs tasks on multiple processes.
class MultiprocTaskRunner : public ftl::TaskRunner {
 public:
  MultiprocTaskRunner(uint32_t thread_count);

  ~MultiprocTaskRunner();

  // TaskRunner implementation.
  void PostTask(ftl::Closure task) override;

  void PostTaskForTime(ftl::Closure task, ftl::TimePoint target_time) override;

  void PostDelayedTask(ftl::Closure task, ftl::TimeDelta delay) override;

  bool RunsTasksOnCurrentThread() override;

 private:
  void Worker(uint32_t thread_number);

  void QueuePacket(uint64_t key, void* payload = nullptr);

  mx::port port_;
  std::vector<std::thread> threads_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MultiprocTaskRunner);
};

}  // namespace media

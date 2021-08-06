// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_ANALYTICS_EXECUTOR_H_
#define SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_ANALYTICS_EXECUTOR_H_

#include <chrono>
#include <mutex>
#include <thread>

#include "lib/fpromise/promise.h"
#include "lib/fpromise/scope.h"
#include "src/developer/debug/shared/platform_message_loop.h"

namespace analytics::core_dev_tools {

// The goal of this executor:
// - Run a message loop in a separate thread (for sending analytics)
// - When the main thread exits:
//   - If all the tasks are finished, quit the loop immediately
//   - Otherwise, quit the loop after all tasks are finished or reaching a timeout, whichever
//     happens earlier.
class AnalyticsExecutor : public fpromise::executor {
 public:
  AnalyticsExecutor();

  explicit AnalyticsExecutor(int64_t quit_timeout_soft_ms);
  AnalyticsExecutor(const AnalyticsExecutor&) = delete;
  AnalyticsExecutor& operator=(const AnalyticsExecutor&) = delete;

  ~AnalyticsExecutor() override;
  void schedule_task(fpromise::pending_task task) override;

 private:
  void RunLoop();

  std::chrono::milliseconds quit_timeout_soft_;
  std::mutex mutex_;
  uint64_t task_count_ = 0;
  bool should_quit_ = false;
  debug::PlatformMessageLoop loop_;
  // scope_ must be declared after loop_ so that it will be destructed before loop_.
  // This is to make sure promises that are not fulfilled before timeout are abandoned before
  // destruction of the message loop.
  fpromise::scope scope_;
  std::thread thread_;
};

}  // namespace analytics::core_dev_tools

#endif  // SRC_LIB_ANALYTICS_CPP_CORE_DEV_TOOLS_ANALYTICS_EXECUTOR_H_

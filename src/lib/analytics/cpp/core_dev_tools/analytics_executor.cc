// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/analytics/cpp/core_dev_tools/analytics_executor.h"

#include <lib/fpromise/promise.h>

#include <algorithm>

namespace analytics::core_dev_tools {

AnalyticsExecutor::AnalyticsExecutor() : AnalyticsExecutor(0) {}

AnalyticsExecutor::AnalyticsExecutor(int64_t quit_timeout_soft_ms)
    : quit_timeout_soft_(quit_timeout_soft_ms) {
  thread_ = std::thread(&AnalyticsExecutor::RunLoop, this);
}

// Thread safety analysis
// ======================
// member variables subject to race conditions:
//     uint64_t task_count_;
//     bool should_quit_;
//     debug_ipc::PlatformMessageLoop loop_;
//
// The use of loop_ follows thread safety guide in the comments in message_loop.h
//
// The variable task_count_ is read in ~AnalyticsExecutor() at the line
//     (A1) if (task_count_ == 0) {
// and incremented in schedule_task(fpromise::pending_task task) at the line
//     (A2) ++task_count_;
// and decremented in a promise created in schedule_task(fpromise::pending_task task) at the line
//     (A3) if (!--task_count_ && should_quit_) {
// (A1) and (A2) are in the main thread, while (A3) is in the analytics thread.
// (A1) should always happen after all the possible (A2) since (A1) is in the destructor.
// Therefore, all the possible race conditions are:
// - (A1) races with (A3): will be analyzed later with should_quit_.
// - (A2) races with (A3): both of them are protected by a mutex, so the increment and decrement
// will be correct. And since each (A2) happens before its corresponding (A3), we don't need to
// worry about integer underflow.
//
// The variable should_quit_ is set in ~AnalyticsExecutor() at line
//     (B1) should_quit_ = true;
// and is read in schedule_task(fpromise::pending_task task) at line
//     (B2)/(A3) if (!--task_count_ && should_quit_) {
// Note that in both ~AnalyticsExecutor() and schedule_task(fpromise::pending_task task),
// task_count_ and should_quit_ are guarded together by mutex_. Therefore, the race condition here
// only have the following two cases (note that (A1)(B1) only happen once while (A3)/(B2) could
// happen many times):
// - (A1)(B1) happens before some (A3)/(B2): in this case things happen in the following order
//   - (B1): should_quit <- true
//   - (A1): task_count_ > 0
//   - (A3)/(B2): (maybe a few times)
//   - last (A3)/(B2): (!--task_count_ && should_quit_) is true, run QuitNow()
// - (A1)(B1) happens after all the (A3)/(B2):
//   - last (A3)/(B2): task_count <- 0, but should_quit is false
//   - (B1) should_quit_ <- true
//   - (A1): task_count == 0, run QuitNow()
// In both cases, the goal for the executor is met.

AnalyticsExecutor::~AnalyticsExecutor() {
  {
    std::scoped_lock lock(mutex_);
    should_quit_ = true;
    if (task_count_ == 0) {
      loop_.PostTask(FROM_HERE, fpromise::make_promise([this] { loop_.QuitNow(); }));
    } else if (quit_timeout_soft_ >= std::chrono::milliseconds::zero()) {
      loop_.PostTimer(FROM_HERE, quit_timeout_soft_.count(), [this] { loop_.QuitNow(); });
    }
  }
  thread_.join();
}

void AnalyticsExecutor::schedule_task(fpromise::pending_task task) {
  {
    std::scoped_lock lock(mutex_);
    ++task_count_;
  }
  loop_.schedule_task(task.take_promise()
                          .then([this](fpromise::result<>& /*result*/) {
                            std::scoped_lock lock(mutex_);
                            if (!--task_count_ && should_quit_) {
                              loop_.QuitNow();
                            }
                          })
                          .wrap_with(scope_));
}

void AnalyticsExecutor::RunLoop() {
  std::string error_message;
  if (!loop_.Init(&error_message)) {
    fprintf(stderr, "%s", error_message.c_str());
    return;
  }
  loop_.Run();
  loop_.Cleanup();
}

}  // namespace analytics::core_dev_tools

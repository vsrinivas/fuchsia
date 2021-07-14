// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_STORAGE_VFS_CPP_JOURNAL_BACKGROUND_EXECUTOR_H_
#define SRC_LIB_STORAGE_VFS_CPP_JOURNAL_BACKGROUND_EXECUTOR_H_

#include <lib/fpromise/promise.h>
#include <lib/fpromise/single_threaded_executor.h>
#include <threads.h>
#include <zircon/compiler.h>
#include <zircon/types.h>

#include <memory>
#include <mutex>

namespace fs {

// A generic task executor, capable of running only when work is available until destroyed. Tasks
// added to the BackgroundExecutor are executed on a single thread.
//
// This class is not assignable, copyable, or moveable. This class is thread-safe.
class BackgroundExecutor final : public fpromise::executor {
 public:
  BackgroundExecutor();
  BackgroundExecutor(const BackgroundExecutor&) = delete;
  BackgroundExecutor(BackgroundExecutor&&) = delete;
  BackgroundExecutor& operator=(const BackgroundExecutor&) = delete;
  BackgroundExecutor& operator=(BackgroundExecutor&&) = delete;
  ~BackgroundExecutor() override;

  // Schedules a unit of work to be processed in a background thread.
  //
  // All tasks scheduled to |BackgroundExecutor| via this method are not serialized.
  //
  // Serialization may be enforced by wrapping incoming objects with a fpromise::sequencer object,
  // if desired.
  void schedule_task(fpromise::pending_task task) final {
    executor_.schedule_task(std::move(task));
  }

 private:
  // Executor which dispatches all scheduled tasks.
  fpromise::single_threaded_executor executor_;
  // Thread which periodically updates all pending data allocations.
  thrd_t thrd_;

  // Protects access to the "terminate_" task.
  //
  // Used infrequently -- only on setup and teardown.
  std::mutex lock_;

  // An "always scheduled" suspended task, which is resumed during destruction to finish running all
  // tasks and then exit.
  fpromise::suspended_task terminate_ __TA_GUARDED(lock_);
  bool should_terminate_ __TA_GUARDED(lock_) = false;
};

}  // namespace fs

#endif  // SRC_LIB_STORAGE_VFS_CPP_JOURNAL_BACKGROUND_EXECUTOR_H_

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_TASKS_INCOMING_TASK_QUEUE_H_
#define LIB_MTL_TASKS_INCOMING_TASK_QUEUE_H_

#include <utility>
#include <vector>

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/functional/closure.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/memory/ref_counted.h"
#include "lib/fxl/synchronization/mutex.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/fxl/tasks/task_runner.h"
#include "lib/fxl/time/time_point.h"

namespace mtl {
namespace internal {

class FXL_EXPORT TaskQueueDelegate {
 public:
  virtual void PostTask(fxl::Closure task, fxl::TimePoint target_time) = 0;
  virtual bool RunsTasksOnCurrentThread() = 0;

 protected:
  virtual ~TaskQueueDelegate();
};

// Receives tasks from multiple threads and buffers them until a delegate
// is ready to receive them.
//
// This object is threadsafe.
class FXL_EXPORT IncomingTaskQueue : public fxl::TaskRunner {
 public:
  IncomingTaskQueue();
  ~IncomingTaskQueue() override;

  // |TaskRunner| implementation:
  void PostTask(fxl::Closure task) override;
  void PostTaskForTime(fxl::Closure task, fxl::TimePoint target_time) override;
  void PostDelayedTask(fxl::Closure task, fxl::TimeDelta delay) override;
  bool RunsTasksOnCurrentThread() override;

  // Sets the delegate and schedules all pending tasks with it.
  void InitDelegate(TaskQueueDelegate* delegate);

  // Clears the delegate and drops all later incoming tasks.
  void ClearDelegate();

 private:
  void AddTask(fxl::Closure task, fxl::TimePoint target_time);

  using Task = std::pair<fxl::Closure, fxl::TimePoint>;

  fxl::Mutex mutex_;
  std::vector<Task> incoming_queue_ FXL_GUARDED_BY(mutex_);
  TaskQueueDelegate* delegate_ FXL_GUARDED_BY(mutex_) = nullptr;
  bool drop_incoming_tasks_ FXL_GUARDED_BY(mutex_) = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(IncomingTaskQueue);
};

}  // namespace internal
}  // namespace mtl

#endif  // LIB_MTL_TASKS_INCOMING_TASK_QUEUE_H_

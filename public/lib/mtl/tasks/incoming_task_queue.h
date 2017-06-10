// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_TASKS_INCOMING_TASK_QUEUE_H_
#define LIB_MTL_TASKS_INCOMING_TASK_QUEUE_H_

#include <utility>
#include <vector>

#include "lib/ftl/ftl_export.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/synchronization/mutex.h"
#include "lib/ftl/synchronization/thread_annotations.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/ftl/time/time_point.h"

namespace mtl {
namespace internal {

class FTL_EXPORT TaskQueueDelegate {
 public:
  virtual void PostTask(ftl::Closure task, ftl::TimePoint target_time) = 0;
  virtual bool RunsTasksOnCurrentThread() = 0;

 protected:
  virtual ~TaskQueueDelegate();
};

// Receives tasks from multiple threads and buffers them until a delegate
// is ready to receive them.
//
// This object is threadsafe.
class FTL_EXPORT IncomingTaskQueue : public ftl::TaskRunner {
 public:
  IncomingTaskQueue();
  ~IncomingTaskQueue() override;

  // |TaskRunner| implementation:
  void PostTask(ftl::Closure task) override;
  void PostTaskForTime(ftl::Closure task, ftl::TimePoint target_time) override;
  void PostDelayedTask(ftl::Closure task, ftl::TimeDelta delay) override;
  bool RunsTasksOnCurrentThread() override;

  // Sets the delegate and schedules all pending tasks with it.
  void InitDelegate(TaskQueueDelegate* delegate);

  // Clears the delegate and drops all later incoming tasks.
  void ClearDelegate();

 private:
  void AddTask(ftl::Closure task, ftl::TimePoint target_time);

  using Task = std::pair<ftl::Closure, ftl::TimePoint>;

  ftl::Mutex mutex_;
  std::vector<Task> incoming_queue_ FTL_GUARDED_BY(mutex_);
  TaskQueueDelegate* delegate_ FTL_GUARDED_BY(mutex_) = nullptr;
  bool drop_incoming_tasks_ FTL_GUARDED_BY(mutex_) = false;

  FTL_DISALLOW_COPY_AND_ASSIGN(IncomingTaskQueue);
};

}  // namespace internal
}  // namespace mtl

#endif  // LIB_MTL_TASKS_INCOMING_TASK_QUEUE_H_

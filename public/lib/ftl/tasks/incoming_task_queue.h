// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_TASKS_INCOMING_TASK_QUEUE_H_
#define LIB_FTL_TASKS_INCOMING_TASK_QUEUE_H_

#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/synchronization/mutex.h"
#include "lib/ftl/synchronization/thread_annotations.h"
#include "lib/ftl/tasks/pending_task.h"
#include "lib/ftl/tasks/task_runner.h"

namespace ftl {
namespace internal {

class TaskQueueDelegate {
 public:
  virtual void ScheduleDrainIncomingTasks() = 0;

 protected:
  virtual ~TaskQueueDelegate();
};

// Receives tasks from multiple threads and provides them for a |MessageLoop|.
//
// This object is threadsafe.
class IncomingTaskQueue : public TaskRunner {
 public:
  explicit IncomingTaskQueue(TaskQueueDelegate* delegate);
  ~IncomingTaskQueue() override;

  // |TaskRunner| implementation:
  void PostTask(Closure task) override;
  void PostDelayedTask(Closure task, Duration delay) override;

  TaskQueue TakeTaskQueue();
  void ClearDelegate();

 private:
  void AddTask(Closure task, Duration delay);

  Mutex mutex_;
  TaskQueue incoming_queue_ FTL_GUARDED_BY(mutex_);
  TaskQueueDelegate* delegate_ FTL_GUARDED_BY(mutex_);

  unsigned int next_sequence_number_ FTL_GUARDED_BY(mutex_) = 0;
  bool drain_scheduled_ FTL_GUARDED_BY(mutex_) = false;

  FTL_DISALLOW_COPY_AND_ASSIGN(IncomingTaskQueue);
};

}  // namespace internal
}  // namespace ftl

#endif  // LIB_FTL_TASKS_INCOMING_TASK_QUEUE_H_

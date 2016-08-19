// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MTL_TASKS_INCOMING_TASK_QUEUE_H_
#define LIB_MTL_TASKS_INCOMING_TASK_QUEUE_H_

#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_counted.h"
#include "lib/ftl/synchronization/mutex.h"
#include "lib/ftl/synchronization/thread_annotations.h"
#include "lib/ftl/tasks/task_runner.h"
#include "lib/mtl/tasks/pending_task.h"

namespace mtl {
namespace internal {

class TaskQueueDelegate {
 public:
  virtual void ScheduleDrainIncomingTasks() = 0;
  virtual bool RunsTasksOnCurrentThread() = 0;

 protected:
  virtual ~TaskQueueDelegate();
};

// Receives tasks from multiple threads and provides them for a |MessageLoop|.
//
// This object is threadsafe.
class IncomingTaskQueue : public ftl::TaskRunner {
 public:
  IncomingTaskQueue();
  ~IncomingTaskQueue() override;

  // |TaskRunner| implementation:
  void PostTask(ftl::Closure task) override;
  void PostDelayedTask(ftl::Closure task, ftl::TimeDelta delay) override;
  bool RunsTasksOnCurrentThread() override;

  TaskQueue TakeTaskQueue();

  void InitDelegate(TaskQueueDelegate* delegate);
  void ClearDelegate();

 private:
  void AddTask(ftl::Closure task, ftl::TimeDelta delay);

  ftl::Mutex mutex_;
  TaskQueue incoming_queue_ FTL_GUARDED_BY(mutex_);
  TaskQueueDelegate* delegate_ FTL_GUARDED_BY(mutex_) = nullptr;

  unsigned int next_sequence_number_ FTL_GUARDED_BY(mutex_) = 0;
  bool drop_incoming_tasks_ FTL_GUARDED_BY(mutex_) = false;

  FTL_DISALLOW_COPY_AND_ASSIGN(IncomingTaskQueue);
};

}  // namespace internal
}  // namespace mtl

#endif  // LIB_MTL_TASKS_INCOMING_TASK_QUEUE_H_

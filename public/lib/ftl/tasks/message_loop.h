// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FTL_TASKS_MESSAGE_LOOP_H_
#define LIB_FTL_TASKS_MESSAGE_LOOP_H_

#include <memory>
#include <queue>

#include "lib/ftl/macros.h"
#include "lib/ftl/memory/ref_ptr.h"
#include "lib/ftl/synchronization/waitable_event.h"
#include "lib/ftl/tasks/incoming_task_queue.h"
#include "lib/ftl/tasks/pending_task.h"
#include "lib/ftl/tasks/task_runner.h"

namespace ftl {

class MessageLoop : public internal::TaskQueueDelegate {
 public:
  MessageLoop();
  explicit MessageLoop(RefPtr<internal::IncomingTaskQueue> incoming_tasks);
  ~MessageLoop() override;

  static MessageLoop* GetCurrent();

  TaskRunner* task_runner() const { return incoming_tasks_.get(); }

  void Run();
  void QuitNow();

 private:
  // |internal::TaskQueueDelegate| implementation:
  void ScheduleDrainIncomingTasks() override;

  void ReloadQueue();
  TimePoint RunReadyTasks();
  void RunTask(const internal::PendingTask& pending_task);

  AutoResetWaitableEvent event_;
  RefPtr<internal::IncomingTaskQueue> incoming_tasks_;

  bool should_quit_ = false;
  std::priority_queue<internal::PendingTask> queue_;

  // A recent snapshot of Time::Now(), used to check delayed_work_queue_.
  TimePoint recent_time_;

  FTL_DISALLOW_COPY_AND_ASSIGN(MessageLoop);
};

}  // namespace ftl

#endif  // LIB_FTL_TASKS_MESSAGE_LOOP_H_

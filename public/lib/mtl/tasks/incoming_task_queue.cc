// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/tasks/incoming_task_queue.h"

namespace mtl {
namespace internal {

TaskQueueDelegate::~TaskQueueDelegate() {}

IncomingTaskQueue::IncomingTaskQueue() {}

IncomingTaskQueue::~IncomingTaskQueue() {}

void IncomingTaskQueue::PostTask(ftl::Closure task) {
  AddTask(std::move(task), ftl::TimePoint());
}

void IncomingTaskQueue::PostTaskForTime(ftl::Closure task,
                                        ftl::TimePoint target_time) {
  AddTask(std::move(task), target_time);
}

void IncomingTaskQueue::PostDelayedTask(ftl::Closure task,
                                        ftl::TimeDelta delay) {
  AddTask(std::move(task), delay > ftl::TimeDelta::Zero()
                               ? ftl::TimePoint::Now() + delay
                               : ftl::TimePoint());
}

void IncomingTaskQueue::AddTask(ftl::Closure task, ftl::TimePoint target_time) {
  ftl::MutexLocker locker(&mutex_);

  if (drop_incoming_tasks_)
    return;
  if (delegate_) {
    delegate_->PostTask(std::move(task), target_time);
  } else {
    incoming_queue_.emplace_back(std::move(task), target_time);
  }
}

bool IncomingTaskQueue::RunsTasksOnCurrentThread() {
  ftl::MutexLocker locker(&mutex_);
  return delegate_ && delegate_->RunsTasksOnCurrentThread();
}

void IncomingTaskQueue::InitDelegate(TaskQueueDelegate* delegate) {
  FTL_DCHECK(delegate);

  ftl::MutexLocker locker(&mutex_);
  FTL_DCHECK(!drop_incoming_tasks_);

  delegate_ = delegate;
  for (auto& task : incoming_queue_)
    delegate_->PostTask(std::move(task.first), task.second);
  incoming_queue_.clear();
}

void IncomingTaskQueue::ClearDelegate() {
  ftl::MutexLocker locker(&mutex_);

  FTL_DCHECK(!drop_incoming_tasks_);
  drop_incoming_tasks_ = true;
  delegate_ = nullptr;
}

}  // namespace internal
}  // namespace mtl

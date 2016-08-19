// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/tasks/incoming_task_queue.h"

#include <utility>

namespace mtl {
namespace internal {
namespace {

ftl::TimePoint GetTargetTime(ftl::TimeDelta delay) {
  if (delay > ftl::TimeDelta::Zero())
    return ftl::TimePoint::Now() + delay;
  FTL_DCHECK(delay == ftl::TimeDelta::Zero());
  return ftl::TimePoint();
}

}  // namespace

TaskQueueDelegate::~TaskQueueDelegate() {}

IncomingTaskQueue::IncomingTaskQueue() {}

IncomingTaskQueue::~IncomingTaskQueue() {}

void IncomingTaskQueue::PostTask(ftl::Closure task) {
  AddTask(std::move(task), ftl::TimeDelta::Zero());
}

void IncomingTaskQueue::PostDelayedTask(ftl::Closure task,
                                        ftl::TimeDelta delay) {
  AddTask(std::move(task), delay);
}

void IncomingTaskQueue::AddTask(ftl::Closure task, ftl::TimeDelta delay) {
  ftl::TimePoint target_time = GetTargetTime(delay);

  ftl::MutexLocker locker(&mutex_);

  if (drop_incoming_tasks_)
    return;

  const bool was_empty = incoming_queue_.empty();
  incoming_queue_.emplace_back(std::move(task), target_time,
                               next_sequence_number_++);

  if (was_empty && delegate_) {
    // Notice that we're still holding mutex here. Chromium uses a reader/writer
    // lock to avoid having to hold the queue mutex when calling back into the
    // delegate.
    delegate_->ScheduleDrainIncomingTasks();
  }
}

bool IncomingTaskQueue::RunsTasksOnCurrentThread() {
  ftl::MutexLocker locker(&mutex_);
  return delegate_ && delegate_->RunsTasksOnCurrentThread();
}

TaskQueue IncomingTaskQueue::TakeTaskQueue() {
  TaskQueue result;
  {
    ftl::MutexLocker locker(&mutex_);
    incoming_queue_.swap(result);
  }
  return result;
}

void IncomingTaskQueue::InitDelegate(TaskQueueDelegate* delegate) {
  ftl::MutexLocker locker(&mutex_);
  FTL_DCHECK(!drop_incoming_tasks_);
  delegate_ = delegate;
}

void IncomingTaskQueue::ClearDelegate() {
  ftl::MutexLocker locker(&mutex_);
  FTL_DCHECK(!drop_incoming_tasks_);
  drop_incoming_tasks_ = true;
  delegate_ = nullptr;
}

}  // namespace internal
}  // namespace mtl

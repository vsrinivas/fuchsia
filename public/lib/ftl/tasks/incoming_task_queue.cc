// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ftl/tasks/incoming_task_queue.h"

#include <utility>

namespace ftl {
namespace internal {
namespace {

TimePoint GetTargetTime(TimeDelta delay) {
  if (delay > TimeDelta::Zero())
    return TimePoint::Now() + delay;
  FTL_DCHECK(delay == TimeDelta::Zero());
  return TimePoint();
}

}  // namespace

TaskQueueDelegate::~TaskQueueDelegate() {}

IncomingTaskQueue::IncomingTaskQueue() {}

IncomingTaskQueue::~IncomingTaskQueue() {}

void IncomingTaskQueue::PostTask(Closure task) {
  AddTask(std::move(task), TimeDelta::Zero());
}

void IncomingTaskQueue::PostDelayedTask(Closure task, TimeDelta delay) {
  AddTask(std::move(task), delay);
}

void IncomingTaskQueue::AddTask(Closure task, TimeDelta delay) {
  TimePoint target_time = GetTargetTime(delay);

  MutexLocker locker(&mutex_);

  if (drop_incoming_tasks_)
    return;

  const bool was_empty = incoming_queue_.empty();
  incoming_queue_.emplace_back(std::move(task), target_time,
                               next_sequence_number_++);

  if (was_empty && !drain_scheduled_ && delegate_) {
    drain_scheduled_ = true;
    // Notice that we're still holding mutex here. Chromium uses a reader/writer
    // lock to avoid having to hold the queue mutex when calling back into the
    // delegate.
    delegate_->ScheduleDrainIncomingTasks();
  }
}

TaskQueue IncomingTaskQueue::TakeTaskQueue() {
  TaskQueue result;
  {
    MutexLocker locker(&mutex_);
    if (incoming_queue_.empty())
      drain_scheduled_ = false;
    else
      incoming_queue_.swap(result);
  }
  return result;
}

void IncomingTaskQueue::InitDelegate(TaskQueueDelegate* delegate) {
  MutexLocker locker(&mutex_);
  FTL_DCHECK(!drop_incoming_tasks_);
  delegate_ = delegate;
}

void IncomingTaskQueue::ClearDelegate() {
  MutexLocker locker(&mutex_);
  FTL_DCHECK(!drop_incoming_tasks_);
  drop_incoming_tasks_ = true;
  delegate_ = nullptr;
}

}  // namespace internal
}  // namespace ftl

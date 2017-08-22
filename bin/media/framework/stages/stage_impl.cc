// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/framework/stages/stage_impl.h"

#include "lib/ftl/logging.h"

namespace media {

StageImpl::StageImpl() : update_counter_(0) {}

StageImpl::~StageImpl() {}

void StageImpl::UnprepareInput(size_t index) {}

void StageImpl::UnprepareOutput(size_t index,
                                const UpstreamCallback& callback) {}

void StageImpl::NeedsUpdate() {
  if (++update_counter_ == 1) {
    // This stage has no update pending in the task queue or running.
    PostTask([this]() { UpdateUntilDone(); });
  } else {
    // This stage already has an update either pending in the task queue or
    // running. Set the counter to 2 so it will never go out of range. We don't
    // set it to 1, because, if we're in |UpdateUntilDone|, that would indicate
    // we no longer need to update.
    update_counter_ = 2;
  }
}

void StageImpl::UpdateUntilDone() {
  while (true) {
    // Set the counter to 1. If it's still 1 after we updated, we're done.
    // Otherwise, we need to update more.
    update_counter_ = 1;

    Update();

    // Quit if the counter is still at 1, otherwise update again.
    uint32_t expected = 1;
    if (update_counter_.compare_exchange_strong(expected, 0)) {
      break;
    }
  }
}

void StageImpl::SetTaskRunner(ftl::RefPtr<ftl::TaskRunner> task_runner) {
  FTL_DCHECK(task_runner);
  ftl::RefPtr<ftl::TaskRunner> node_task_runner = GetNodeTaskRunner();
  task_runner_ = node_task_runner ? node_task_runner : task_runner;
}

void StageImpl::PostTask(const ftl::Closure& task) {
  FTL_DCHECK(task);

  {
    ftl::MutexLocker locker(&tasks_mutex_);
    tasks_.push(task);
    if (tasks_.size() != 1) {
      // Don't need to run tasks, because there were already tasks in
      // the queue.
      return;
    }
  }

  FTL_DCHECK(task_runner_);
  task_runner_->PostTask([this]() { RunTasks(); });
}

void StageImpl::RunTasks() {
  tasks_mutex_.Lock();

  while (!tasks_.empty()) {
    ftl::Closure& task = tasks_.front();
    tasks_mutex_.Unlock();
    task();
    tasks_mutex_.Lock();
    tasks_.pop();
  }

  tasks_mutex_.Unlock();
}

}  // namespace media

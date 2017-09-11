// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/framework/stages/stage_impl.h"

#include "lib/fxl/logging.h"

namespace media {

StageImpl::StageImpl() : update_counter_(0) {}

StageImpl::~StageImpl() {}

void StageImpl::ShutDown() {
  {
    fxl::MutexLocker locker(&tasks_mutex_);
    tasks_suspended_ = true;
  }

  GenericNode* generic_node = GetGenericNode();
  FXL_DCHECK(generic_node);

  generic_node->SetGenericStage(nullptr);

  fxl::RefPtr<fxl::TaskRunner> node_task_runner = generic_node->GetTaskRunner();
  if (node_task_runner) {
    // Release the node in the node-provided task runner.
    PostShutdownTask([this]() { ReleaseNode(); });
  } else {
    // Release the node on this thread.
    ReleaseNode();
  }
}

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

void StageImpl::Acquire(const fxl::Closure& callback) {
  PostTask([this, callback]() {
    {
      fxl::MutexLocker locker(&tasks_mutex_);
      tasks_suspended_ = true;
    }

    callback();
  });
}

void StageImpl::Release() {
  {
    fxl::MutexLocker locker(&tasks_mutex_);
    tasks_suspended_ = false;
    if (tasks_.empty()) {
      // Don't need to run tasks.
      return;
    }
  }

  FXL_DCHECK(task_runner_);
  task_runner_->PostTask([shared_this = shared_from_this()]() {
    shared_this->RunTasks();
  });
}

void StageImpl::SetTaskRunner(fxl::RefPtr<fxl::TaskRunner> task_runner) {
  FXL_DCHECK(task_runner);
  fxl::RefPtr<fxl::TaskRunner> node_task_runner =
      GetGenericNode()->GetTaskRunner();
  task_runner_ = node_task_runner ? node_task_runner : task_runner;
}

void StageImpl::PostTask(const fxl::Closure& task) {
  FXL_DCHECK(task);

  {
    fxl::MutexLocker locker(&tasks_mutex_);
    tasks_.push(task);
    if (tasks_.size() != 1 || tasks_suspended_) {
      // Don't need to run tasks, either because there were already tasks in
      // the queue or because task execution is suspended.
      return;
    }
  }

  FXL_DCHECK(task_runner_);
  task_runner_->PostTask([shared_this = shared_from_this()]() {
    shared_this->RunTasks();
  });
}

void StageImpl::PostShutdownTask(fxl::Closure task) {
  FXL_DCHECK(task_runner_);
  task_runner_->PostTask(
      [ shared_this = shared_from_this(), task ]() { task(); });
}

void StageImpl::RunTasks() {
  tasks_mutex_.Lock();

  while (!tasks_.empty() && !tasks_suspended_) {
    fxl::Closure& task = tasks_.front();
    tasks_mutex_.Unlock();
    task();
    // The closure may be keeping objects alive. Destroy it here so those
    // objects are destroyed with the mutex unlocked. It's OK to do this,
    // because this method is the only consumer of tasks from the queue, and
    // this method will not be re-entered.
    task = nullptr;
    tasks_mutex_.Lock();
    tasks_.pop();
  }

  tasks_mutex_.Unlock();
}

}  // namespace media

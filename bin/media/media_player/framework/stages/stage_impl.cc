// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/framework/stages/stage_impl.h"

#include <lib/async/cpp/task.h>

#include "lib/fxl/logging.h"

namespace media_player {

StageImpl::StageImpl() : update_counter_(0) {}

StageImpl::~StageImpl() {}

void StageImpl::OnShutDown() {}

void StageImpl::UnprepareInput(size_t index) {}

void StageImpl::UnprepareOutput(size_t index) {}

void StageImpl::ShutDown() {
  {
    std::lock_guard<std::mutex> locker(tasks_mutex_);
    while (!tasks_.empty()) {
      tasks_.pop();
    }
  }

  OnShutDown();

  GenericNode* generic_node = GetGenericNode();
  FXL_DCHECK(generic_node);

  generic_node->SetGenericStage(nullptr);
}

void StageImpl::NeedsUpdate() {
  // Atomically preincrement the update counter. If we get the value 1, that
  // means the counter was zero, and we need to post an update. If we get
  // anything else, |UpdateUntilDone| is already running. In that case, we know
  // |UpdateUntilDone| will run |Update| after the increment occurred.
  if (++update_counter_ == 1) {
    // This stage has no update pending in the task queue or running.
    PostTask([this]() { UpdateUntilDone(); });
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
      std::lock_guard<std::mutex> locker(tasks_mutex_);
      tasks_suspended_ = true;
    }

    callback();
  });
}

void StageImpl::Release() {
  {
    std::lock_guard<std::mutex> locker(tasks_mutex_);
    tasks_suspended_ = false;
    if (tasks_.empty()) {
      // Don't need to run tasks.
      return;
    }
  }

  FXL_DCHECK(async_);
  async::PostTask(async_, [shared_this = shared_from_this()]() {
    shared_this->RunTasks();
  });
}

void StageImpl::SetAsync(async_t* async) {
  FXL_DCHECK(async);
  async_ = async;
}

void StageImpl::PostTask(const fxl::Closure& task) {
  FXL_DCHECK(task);

  {
    std::lock_guard<std::mutex> locker(tasks_mutex_);
    tasks_.push(task);
    if (tasks_.size() != 1 || tasks_suspended_) {
      // Don't need to run tasks, either because there were already tasks in
      // the queue or because task execution is suspended.
      return;
    }
  }

  FXL_DCHECK(async_);
  async::PostTask(async_, [shared_this = shared_from_this()]() {
    shared_this->RunTasks();
  });
}

void StageImpl::PostShutdownTask(fxl::Closure task) {
  FXL_DCHECK(async_);
  async::PostTask(async_,
                  [shared_this = shared_from_this(), task]() { task(); });
}

void StageImpl::RunTasks() {
  tasks_mutex_.lock();

  while (!tasks_.empty() && !tasks_suspended_) {
    fxl::Closure& task = tasks_.front();
    tasks_mutex_.unlock();
    task();
    // The closure may be keeping objects alive. Destroy it here so those
    // objects are destroyed with the mutex unlocked. It's OK to do this,
    // because this method is the only consumer of tasks from the queue, and
    // this method will not be re-entered.
    task = nullptr;
    tasks_mutex_.lock();
    tasks_.pop();
  }

  tasks_mutex_.unlock();
}

}  // namespace media_player

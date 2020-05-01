// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "task.h"

Task::Task(async_dispatcher_t* dispatcher, Completion completion, bool post_on_create)
    : completion_(std::move(completion)), dispatcher_(dispatcher) {
  if (post_on_create) {
    ZX_ASSERT(async_task_.Post(dispatcher_) == ZX_OK);
  }
}

Task::~Task() { ZX_ASSERT(dependents_.is_empty()); }

void Task::ExecuteTask(async_dispatcher_t* dispatcher, async::TaskBase* task, zx_status_t status) {
  // If we've already completed, we have no more work to do.
  // If we have outstanding dependencies, we'll be rescheduled when they're done
  if (std::holds_alternative<zx_status_t>(status_) ||
      finished_dependencies_count_ != total_dependencies_count_) {
    return;
  }

  Run();
}

// Called to record a new dependency
void Task::AddDependency(const fbl::RefPtr<Task>& dependency) {
  ZX_ASSERT_MSG(!dependency->is_completed(), "Tried adding already complete task %s as a dep of %s",
                dependency->TaskDescription().c_str(), TaskDescription().c_str());

  dependencies_.push_back(dependency.get());
  dependency->self_ = dependency;
  dependency->RegisterDependent(fbl::RefPtr(this));
}

void Task::RegisterDependent(fbl::RefPtr<Task> dependent) {
  ++dependent->total_dependencies_count_;

  // Check if we're already completed
  auto* status = std::get_if<zx_status_t>(&status_);
  if (status != nullptr) {
    return dependent->DependencyComplete(this, *status);
  }

  dependents_.push_back(std::move(dependent));
}

void Task::DependencyComplete(const Task* dependency, zx_status_t status) {
  ++finished_dependencies_count_;
  // If this task is already scheduled to run, we shouldn't try to run it again.
  if (!is_pending()) {
    if (finished_dependencies_count_ == total_dependencies_count_) {
      ZX_ASSERT(async_task_.Post(dispatcher_) == ZX_OK);
    }
  }
  if (status != ZX_OK && std::holds_alternative<Incomplete>(status_)) {
    DependencyFailed(status);
  }
  for (unsigned i = 0; i < dependencies_.size(); ++i) {
    if (dependency == dependencies_[i]) {
      dependencies_.erase(i);
      return;
    }
  }
  // The task may have been completed as part of |DependencyFailed|,
  // in which case the list of dependencies would have already been cleared.
  if (!is_completed()) {
    ZX_PANIC("driver_manager: %s could not find dependency %s, already removed?\n",
             TaskDescription().c_str(), dependency->TaskDescription().c_str());
  }
}

void Task::Complete(zx_status_t status) {
  ZX_ASSERT(std::holds_alternative<Incomplete>(status_));

  status_ = status;
  for (auto& dependent : dependents_) {
    dependent->DependencyComplete(this, status);
  }
  dependents_.reset();
  dependencies_.reset();

  // Take an additional self reference while calling the completion, to prevent the completion from
  // dropping the last reference and running the dtor.
  fbl::RefPtr<Task> keep_alive(this);

  Completion completion(std::move(completion_));
  if (completion) {
    completion(status);
  }
  self_.reset();
}

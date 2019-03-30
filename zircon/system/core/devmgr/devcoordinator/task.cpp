// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "task.h"

namespace devmgr {

Task::Task(async_dispatcher_t* dispatcher, Completion completion)
    : completion_(std::move(completion)), dispatcher_(dispatcher) {

    ZX_ASSERT(async_task_.Post(dispatcher_) == ZX_OK);
}

Task::~Task() {
    ZX_ASSERT(dependents_.is_empty());
}

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
    dependency->self_ = dependency;
    dependency->RegisterDependent(fbl::WrapRefPtr(this));
}

void Task::RegisterDependent(fbl::RefPtr<Task> dependent) {
    ++dependent->total_dependencies_count_;

    // Check if we're already completed
    auto* status = std::get_if<zx_status_t>(&status_);
    if (status != nullptr) {
        return dependent->DependencyComplete(*status);
    }

    dependents_.push_back(std::move(dependent));
}

void Task::DependencyComplete(zx_status_t status) {
    ++finished_dependencies_count_;
    if (finished_dependencies_count_ == total_dependencies_count_) {
        ZX_ASSERT(async_task_.Post(dispatcher_) == ZX_OK);
    }
    if (status != ZX_OK && std::holds_alternative<Incomplete>(status_)) {
        DependencyFailed(status);
    }
}

void Task::Complete(zx_status_t status) {
    ZX_ASSERT(std::holds_alternative<Incomplete>(status_));

    status_ = status;
    for (auto& dependent : dependents_) {
        dependent->DependencyComplete(status);
    }
    dependents_.reset();

    Completion completion(std::move(completion_));
    if (completion) {
        completion(status);
    }
    self_.reset();
}

} // namespace devmgr

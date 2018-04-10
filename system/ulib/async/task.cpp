// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>

namespace async {
namespace internal {

struct RetainedTask : public async_task_t {
    RetainedTask(fbl::Closure handler, zx::time deadline)
        : async_task_t{{ASYNC_STATE_INIT}, &RetainedTask::Handler, deadline.get()},
          handler(static_cast<fbl::Closure&&>(handler)) {}

    fbl::Closure handler;

    static void Handler(async_t* async, async_task_t* task, zx_status_t status) {
        auto self = static_cast<RetainedTask*>(task);
        if (status == ZX_OK)
            self->handler();
        delete self;
    }
};

} // namespace internal

zx_status_t PostTask(async_t* async, fbl::Closure handler) {
    return PostTaskForTime(async, static_cast<fbl::Closure&&>(handler),
                           async::Now(async));
}

zx_status_t PostDelayedTask(async_t* async, fbl::Closure handler, zx::duration delay) {
    return PostTaskForTime(async, static_cast<fbl::Closure&&>(handler),
                           async::Now(async) + delay);
}

zx_status_t PostTaskForTime(async_t* async, fbl::Closure handler, zx::time deadline) {
    auto* task = new internal::RetainedTask(static_cast<fbl::Closure&&>(handler), deadline);
    zx_status_t status = async_post_task(async, task);
    if (status != ZX_OK)
        delete task;
    return status;
}

TaskBase::TaskBase(async_task_handler_t* handler)
    : task_{{ASYNC_STATE_INIT}, handler, ZX_TIME_INFINITE} {}

TaskBase::~TaskBase() {
    if (async_) {
        // Failure to cancel here may result in a dangling pointer...
        zx_status_t status = async_cancel_task(async_, &task_);
        ZX_ASSERT_MSG(status == ZX_OK, "status=%d", status);
    }
}

zx_status_t TaskBase::Post(async_t* async) {
    return PostForTime(async, async::Now(async));
}

zx_status_t TaskBase::PostDelayed(async_t* async, zx::duration delay) {
    return PostForTime(async, async::Now(async) + delay);
}

zx_status_t TaskBase::PostForTime(async_t* async, zx::time deadline) {
    if (async_)
        return ZX_ERR_ALREADY_EXISTS;

    async_ = async;
    task_.deadline = deadline.get();
    zx_status_t status = async_post_task(async, &task_);
    if (status != ZX_OK) {
        async_ = nullptr;
    }
    return status;
}

zx_status_t TaskBase::Cancel() {
    if (!async_)
        return ZX_ERR_NOT_FOUND;

    async_t* async = async_;
    async_ = nullptr;
    return async_cancel_task(async, &task_);
}

Task::Task(Handler handler)
    : TaskBase(&Task::CallHandler), handler_(fbl::move(handler)) {}

Task::~Task() = default;

void Task::CallHandler(async_t* async, async_task_t* task, zx_status_t status) {
    auto self = Dispatch<Task>(task);
    self->handler_(async, self, status);
}

TaskClosure::TaskClosure(fbl::Closure handler)
    : TaskBase(&TaskClosure::CallHandler), handler_(fbl::move(handler)) {}

TaskClosure::~TaskClosure() = default;

void TaskClosure::CallHandler(async_t* async, async_task_t* task, zx_status_t status) {
    auto self = Dispatch<TaskClosure>(task); // must do this if status is not ok
    if (status == ZX_OK) {
        self->handler_();
    }
}

} // namespace async

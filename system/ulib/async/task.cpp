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
    return async_post_task_or_report_error(async, task);
}

Task::Task()
    : task_{{ASYNC_STATE_INIT}, &Task::CallHandler, ZX_TIME_INFINITE} {}

Task::Task(Handler handler)
    : task_{{ASYNC_STATE_INIT}, &Task::CallHandler, ZX_TIME_INFINITE},
      handler_(static_cast<Handler&&>(handler)) {}

Task::~Task() = default;

zx_status_t Task::Post(async_t* async) {
    return PostForTime(async, async::Now(async));
}

zx_status_t Task::PostOrReportError(async_t* async) {
    return PostForTimeOrReportError(async, async::Now(async));
}

zx_status_t Task::PostDelayed(async_t* async, zx::duration delay) {
    return PostForTime(async, async::Now(async) + delay);
}

zx_status_t Task::PostDelayedOrReportError(async_t* async, zx::duration delay) {
    return PostForTimeOrReportError(async, async::Now(async) + delay);
}

zx_status_t Task::PostForTime(async_t* async, zx::time deadline) {
    task_.deadline = deadline.get();
    return async_post_task(async, &task_);
}

zx_status_t Task::PostForTimeOrReportError(async_t* async, zx::time deadline) {
    task_.deadline = deadline.get();
    return async_post_task_or_report_error(async, &task_);
}

zx_status_t Task::Cancel(async_t* async) {
    return async_cancel_task(async, &task_);
}

void Task::CallHandler(async_t* async, async_task_t* task,
                       zx_status_t status) {
    static_assert(offsetof(Task, task_) == 0, "");
    auto self = reinterpret_cast<Task*>(task);
    self->handler_(async, self, status);
}

AutoTask::AutoTask()
    : task_{{ASYNC_STATE_INIT}, &AutoTask::CallHandler, ZX_TIME_INFINITE} {}

AutoTask::AutoTask(Handler handler)
    : task_{{ASYNC_STATE_INIT}, &AutoTask::CallHandler, ZX_TIME_INFINITE},
      handler_(static_cast<Handler&&>(handler)) {}

AutoTask::~AutoTask() {
    if (async_) {
        // Failure to cancel here may result in a dangling pointer...
        zx_status_t status = async_cancel_task(async_, &task_);
        ZX_ASSERT_MSG(status == ZX_OK, "status=%d", status);
    }
}

zx_status_t AutoTask::Post(async_t* async) {
    return PostForTime(async, async::Now(async));
}

zx_status_t AutoTask::PostOrReportError(async_t* async) {
    return PostForTimeOrReportError(async, async::Now(async));
}

zx_status_t AutoTask::PostDelayed(async_t* async, zx::duration delay) {
    return PostForTime(async, async::Now(async) + delay);
}

zx_status_t AutoTask::PostDelayedOrReportError(async_t* async, zx::duration delay) {
    return PostForTimeOrReportError(async, async::Now(async) + delay);
}

zx_status_t AutoTask::PostForTime(async_t* async, zx::time deadline) {
    if (async_)
        return ZX_ERR_ALREADY_EXISTS;

    async_ = async;
    task_.deadline = deadline.get();
    zx_status_t status = async_post_task(async, &task_);
    if (status != ZX_OK) {
        async_ = nullptr;
        task_.handler = nullptr;
    }
    return status;
}

zx_status_t AutoTask::PostForTimeOrReportError(async_t* async, zx::time deadline) {
    if (async_)
        return ZX_ERR_ALREADY_EXISTS;

    async_ = async;
    task_.deadline = deadline.get();
    // If an error occurs, the handler will clear |async_| itself.
    return async_post_task_or_report_error(async, &task_);
}

zx_status_t AutoTask::Cancel() {
    if (!async_)
        return ZX_ERR_NOT_FOUND;

    async_t* async = async_;
    async_ = nullptr;
    return async_cancel_task(async, &task_);
}

void AutoTask::CallHandler(async_t* async, async_task_t* task, zx_status_t status) {
    static_assert(offsetof(AutoTask, task_) == 0, "");
    auto self = reinterpret_cast<AutoTask*>(task);
    self->async_ = nullptr;
    self->handler_(async, self, status);
}

} // namespace async

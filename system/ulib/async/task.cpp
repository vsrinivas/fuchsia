// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>

namespace async {

zx_status_t PostTask(async_t* async, fbl::Function<void(void)> closure,
                     zx_time_t deadline) {
    async::Task* task = new async::Task(deadline, ASYNC_FLAG_HANDLE_SHUTDOWN);
    task->set_handler([task, closure = fbl::move(closure)](async_t*, zx_status_t status) {
        if (status == ZX_OK) {
            closure();
        }
        delete task;
        return ASYNC_TASK_FINISHED;
    });
    zx_status_t status = task->Post(async);
    if (status != ZX_OK) {
        delete task;
    }
    return status;
}

Task::Task(zx_time_t deadline, uint32_t flags)
    : async_task_t{{ASYNC_STATE_INIT}, &Task::CallHandler, deadline, flags, {}} {}

Task::~Task() = default;

zx_status_t Task::Post(async_t* async) {
    return async_post_task(async, this);
}

zx_status_t Task::Cancel(async_t* async) {
    return async_cancel_task(async, this);
}

async_task_result_t Task::CallHandler(async_t* async, async_task_t* task,
                                      zx_status_t status) {
    return static_cast<Task*>(task)->handler_(async, status);
}

} // namespace async

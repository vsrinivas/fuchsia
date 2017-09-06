// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <async/auto_task.h>

#include <magenta/assert.h>

namespace async {

AutoTask::AutoTask(async_t* async, mx_time_t deadline, uint32_t flags)
    : async_task_t{{ASYNC_STATE_INIT}, &AutoTask::CallHandler, deadline, flags, {}},
      async_(async) {
    MX_DEBUG_ASSERT(async_);
}

AutoTask::~AutoTask() {
    Cancel();
}

mx_status_t AutoTask::Post() {
    MX_DEBUG_ASSERT(!pending_);

    mx_status_t status = async_post_task(async_, this);
    if (status == MX_OK)
        pending_ = true;

    return status;
}

void AutoTask::Cancel() {
    if (!pending_)
        return;

    mx_status_t status = async_cancel_task(async_, this);
    MX_DEBUG_ASSERT_MSG(status == MX_OK, "status=%d", status);

    pending_ = false;
}

async_task_result_t AutoTask::CallHandler(async_t* async, async_task_t* task,
                                          mx_status_t status) {
    auto self = static_cast<AutoTask*>(task);
    MX_DEBUG_ASSERT(self->pending_);
    self->pending_ = false;

    async_task_result_t result = self->handler_(async, status);
    if (result == ASYNC_TASK_REPEAT && status == MX_OK) {
        MX_DEBUG_ASSERT(!self->pending_);
        self->pending_ = true;
    }
    return result;
}

} // namespace async

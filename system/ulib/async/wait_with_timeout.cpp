// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <async/wait_with_timeout.h>

#include <magenta/assert.h>

namespace async {

WaitWithTimeout::WaitWithTimeout(mx_handle_t object, mx_signals_t trigger,
                                 mx_time_t deadline, uint32_t flags)
    : async_wait_t{{ASYNC_STATE_INIT}, &WaitWithTimeout::WaitHandler, object, trigger, flags, {}},
      async_task_t{{ASYNC_STATE_INIT}, &WaitWithTimeout::TimeoutHandler, deadline, 0u, {}} {}

WaitWithTimeout::~WaitWithTimeout() = default;

mx_status_t WaitWithTimeout::Begin(async_t* async) {
    mx_status_t status = async_begin_wait(async, this);
    if (status == MX_OK && deadline() != MX_TIME_INFINITE) {
        status = async_post_task(async, this);
        if (status != MX_OK) {
            mx_status_t cancel_status = async_cancel_wait(async, this);
            MX_DEBUG_ASSERT_MSG(cancel_status == MX_OK,
                                "cancel_status=%d", cancel_status);
        }
    }
    return status;
}

mx_status_t WaitWithTimeout::Cancel(async_t* async) {
    mx_status_t status = async_cancel_wait(async, this);
    if (status == MX_OK && deadline() != MX_TIME_INFINITE)
        status = async_cancel_task(async, this);
    return status;
}

async_wait_result_t WaitWithTimeout::WaitHandler(async_t* async, async_wait_t* wait,
                                                 mx_status_t status,
                                                 const mx_packet_signal_t* signal) {
    auto self = static_cast<WaitWithTimeout*>(wait);

    // We must cancel the task before calling the handler in case it decides
    // to destroy itself during execution.  If this proves inefficient, we
    // could make timeouts on waits a first class API.
    if (self->deadline() != MX_TIME_INFINITE) {
        mx_status_t cancel_status = async_cancel_task(async, self);
        MX_DEBUG_ASSERT_MSG(cancel_status == MX_OK,
                            "cancel_status=%d", cancel_status);
    }

    async_wait_result_t result = self->handler_(async, status, signal);

    // If the result is ASYNC_WAIT_FINISHED then it's possible that the handler has
    // already destroyed this object.  So take care to only dereference it if the wait
    // is still live.
    if (result == ASYNC_WAIT_AGAIN && status == MX_OK &&
        self->deadline() != MX_TIME_INFINITE) {
        mx_status_t post_status = async_post_task(async, self);
        if (post_status != MX_OK) {
            // The loop is being destroyed.
            MX_DEBUG_ASSERT_MSG(post_status == MX_ERR_BAD_STATE,
                                "post_status=%d", post_status);
            return ASYNC_WAIT_FINISHED;
        }
    }
    return result;
}

async_task_result_t WaitWithTimeout::TimeoutHandler(async_t* async, async_task_t* task,
                                                    mx_status_t status) {
    MX_DEBUG_ASSERT(status == MX_OK);

    auto self = static_cast<WaitWithTimeout*>(task);
    mx_status_t cancel_status = async_cancel_wait(async, self);
    MX_DEBUG_ASSERT_MSG(cancel_status == MX_OK,
                        "cancel_status=%d", cancel_status);

    async_wait_result_t result = self->handler_(async, MX_ERR_TIMED_OUT, nullptr);
    MX_DEBUG_ASSERT(result == ASYNC_WAIT_FINISHED);
    return ASYNC_TASK_FINISHED;
}

} // namespace async

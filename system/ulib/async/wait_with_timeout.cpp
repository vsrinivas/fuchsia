// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/wait_with_timeout.h>

#include <zircon/assert.h>

namespace async {

WaitWithTimeout::WaitWithTimeout(zx_handle_t object, zx_signals_t trigger,
                                 zx::time deadline, uint32_t flags)
    : async_wait_t{{ASYNC_STATE_INIT}, &WaitWithTimeout::WaitHandler, object, trigger, flags, {}},
      async_task_t{{ASYNC_STATE_INIT}, &WaitWithTimeout::TimeoutHandler, deadline.get()} {}

WaitWithTimeout::~WaitWithTimeout() = default;

zx_status_t WaitWithTimeout::Begin(async_t* async) {
    zx_status_t status = async_begin_wait(async, this);
    if (status == ZX_OK && deadline() != zx::time::infinite()) {
        status = async_post_task(async, this);
        if (status != ZX_OK) {
            zx_status_t cancel_status = async_cancel_wait(async, this);
            ZX_DEBUG_ASSERT_MSG(cancel_status == ZX_OK,
                                "cancel_status=%d", cancel_status);
        }
    }
    return status;
}

zx_status_t WaitWithTimeout::Cancel(async_t* async) {
    zx_status_t status = async_cancel_wait(async, this);
    if (status == ZX_OK && deadline() != zx::time::infinite())
        status = async_cancel_task(async, this);
    return status;
}

async_wait_result_t WaitWithTimeout::WaitHandler(async_t* async, async_wait_t* wait,
                                                 zx_status_t status,
                                                 const zx_packet_signal_t* signal) {
    auto self = static_cast<WaitWithTimeout*>(wait);

    // We must cancel the task before calling the handler in case it decides
    // to destroy itself during execution.  If this proves inefficient, we
    // could make timeouts on waits a first class API.
    if (self->deadline() != zx::time::infinite()) {
        zx_status_t cancel_status = async_cancel_task(async, self);
        ZX_DEBUG_ASSERT_MSG(cancel_status == ZX_OK,
                            "cancel_status=%d", cancel_status);
    }

    async_wait_result_t result = self->handler_(async, status, signal);

    // If the result is ASYNC_WAIT_FINISHED then it's possible that the handler has
    // already destroyed this object.  So take care to only dereference it if the wait
    // is still live.
    if (result == ASYNC_WAIT_AGAIN && status == ZX_OK &&
        self->deadline() != zx::time::infinite()) {
        zx_status_t post_status = async_post_task(async, self);
        if (post_status != ZX_OK) {
            // The loop is being destroyed.
            ZX_DEBUG_ASSERT_MSG(post_status == ZX_ERR_BAD_STATE,
                                "post_status=%d", post_status);
            return ASYNC_WAIT_FINISHED;
        }
    }
    return result;
}

void WaitWithTimeout::TimeoutHandler(async_t* async, async_task_t* task,
                                     zx_status_t status) {
    if (status != ZX_OK)
        return;

    auto self = static_cast<WaitWithTimeout*>(task);
    zx_status_t cancel_status = async_cancel_wait(async, self);
    ZX_DEBUG_ASSERT_MSG(cancel_status == ZX_OK,
                        "cancel_status=%d", cancel_status);

    async_wait_result_t result = self->handler_(async, ZX_ERR_TIMED_OUT, nullptr);
    ZX_DEBUG_ASSERT(result == ASYNC_WAIT_FINISHED);
}

} // namespace async

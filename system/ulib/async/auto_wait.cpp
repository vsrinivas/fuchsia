// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <async/cpp/auto_wait.h>

#include <zircon/assert.h>

namespace async {

AutoWait::AutoWait(async_t* async, zx_handle_t object, zx_signals_t trigger, uint32_t flags)
    : async_wait_t{{ASYNC_STATE_INIT}, &AutoWait::CallHandler, object, trigger, flags, {}},
      async_(async) {
    ZX_DEBUG_ASSERT(async_);
}

AutoWait::~AutoWait() {
    Cancel();
}

zx_status_t AutoWait::Begin() {
    ZX_DEBUG_ASSERT(!pending_);

    zx_status_t status = async_begin_wait(async_, this);
    if (status == ZX_OK)
        pending_ = true;

    return status;
}

void AutoWait::Cancel() {
    if (!pending_)
        return;

    zx_status_t status = async_cancel_wait(async_, this);
    ZX_DEBUG_ASSERT_MSG(status == ZX_OK, "status=%d", status);

    pending_ = false;
}

async_wait_result_t AutoWait::CallHandler(async_t* async, async_wait_t* wait,
                                          zx_status_t status, const zx_packet_signal_t* signal) {
    auto self = static_cast<AutoWait*>(wait);
    ZX_DEBUG_ASSERT(self->pending_);
    self->pending_ = false;

    async_wait_result_t result = self->handler_(async, status, signal);
    if (result == ASYNC_WAIT_AGAIN && status == ZX_OK) {
        ZX_DEBUG_ASSERT(!self->pending_);
        self->pending_ = true;
    }
    return result;
}

} // namespace async

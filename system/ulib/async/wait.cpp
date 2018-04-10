// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/wait.h>

namespace async {

WaitBase::WaitBase(zx_handle_t object, zx_signals_t trigger, async_wait_handler_t* handler)
    : wait_{{ASYNC_STATE_INIT}, handler, object, trigger} {}

WaitBase::~WaitBase() {
    if (async_) {
        // Failure to cancel here may result in a dangling pointer...
        zx_status_t status = async_cancel_wait(async_, &wait_);
        ZX_ASSERT_MSG(status == ZX_OK, "status=%d", status);
    }
}

zx_status_t WaitBase::Begin(async_t* async) {
    if (async_)
        return ZX_ERR_ALREADY_EXISTS;

    async_ = async;
    zx_status_t status = async_begin_wait(async, &wait_);
    if (status != ZX_OK) {
        async_ = nullptr;
    }
    return status;
}

zx_status_t WaitBase::Cancel() {
    if (!async_)
        return ZX_ERR_NOT_FOUND;

    async_t* async = async_;
    async_ = nullptr;
    return async_cancel_wait(async, &wait_);
}

Wait::Wait(zx_handle_t object, zx_signals_t trigger, Handler handler)
    : WaitBase(object, trigger, &Wait::CallHandler), handler_(fbl::move(handler)) {}

Wait::~Wait() = default;

void Wait::CallHandler(async_t* async, async_wait_t* wait,
                       zx_status_t status, const zx_packet_signal_t* signal) {
    auto self = Dispatch<Wait>(wait);
    self->handler_(async, self, status, signal);
}

} // namespace async

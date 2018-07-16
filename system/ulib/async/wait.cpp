// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/wait.h>

namespace async {

WaitBase::WaitBase(zx_handle_t object, zx_signals_t trigger, async_wait_handler_t* handler)
    : wait_{{ASYNC_STATE_INIT}, handler, object, trigger} {}

WaitBase::~WaitBase() {
    if (dispatcher_) {
        // Failure to cancel here may result in a dangling pointer...
        zx_status_t status = async_cancel_wait(dispatcher_, &wait_);
        ZX_ASSERT_MSG(status == ZX_OK, "status=%d", status);
    }
}

zx_status_t WaitBase::Begin(async_dispatcher_t* dispatcher) {
    if (dispatcher_)
        return ZX_ERR_ALREADY_EXISTS;

    dispatcher_ = dispatcher;
    zx_status_t status = async_begin_wait(dispatcher, &wait_);
    if (status != ZX_OK) {
        dispatcher_ = nullptr;
    }
    return status;
}

zx_status_t WaitBase::Cancel() {
    if (!dispatcher_)
        return ZX_ERR_NOT_FOUND;

    async_dispatcher_t* dispatcher = dispatcher_;
    dispatcher_ = nullptr;

    zx_status_t status = async_cancel_wait(dispatcher, &wait_);
    // |dispatcher| is required to be single-threaded, Cancel() is
    // only supposed to be called on |dispatcher|'s thread, and
    // we verified that the wait was pending before calling
    // async_cancel_wait(). Assuming that |dispatcher| never queues
    // a wait, |wait_| must have been pending with |dispatcher|.
    ZX_DEBUG_ASSERT(status != ZX_ERR_NOT_FOUND);
    return status;
}

Wait::Wait(zx_handle_t object, zx_signals_t trigger, Handler handler)
    : WaitBase(object, trigger, &Wait::CallHandler), handler_(fbl::move(handler)) {}

Wait::~Wait() = default;

void Wait::CallHandler(async_dispatcher_t* dispatcher, async_wait_t* wait,
                       zx_status_t status, const zx_packet_signal_t* signal) {
    auto self = Dispatch<Wait>(wait);
    self->handler_(dispatcher, self, status, signal);
}

} // namespace async

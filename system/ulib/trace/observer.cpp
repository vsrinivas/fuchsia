// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace/observer.h>

#include <zircon/assert.h>

namespace trace {

TraceObserver::TraceObserver() {
    wait_.set_handler(fbl::BindMember(this, &TraceObserver::Handle));
}

TraceObserver::~TraceObserver() {
    Stop();
}

void TraceObserver::Start(async_t* async, fbl::Closure callback) {
    ZX_DEBUG_ASSERT(async);
    ZX_DEBUG_ASSERT(callback);

    Stop();
    async_ = async;
    callback_ = fbl::move(callback);

    zx_status_t status = zx::event::create(0u, &event_);
    ZX_ASSERT(status == ZX_OK);

    wait_.set_object(event_.get());
    wait_.set_trigger(ZX_EVENT_SIGNALED);
    status = wait_.Begin(async_);
    ZX_DEBUG_ASSERT(status == ZX_OK);

    trace_register_observer(event_.get());
}

void TraceObserver::Stop() {
    if (!async_)
        return;

    trace_unregister_observer(event_.get());

    zx_status_t status = wait_.Cancel(async_);
    ZX_DEBUG_ASSERT(status == ZX_OK);

    async_ = nullptr;
    callback_ = nullptr;
}

async_wait_result_t TraceObserver::Handle(async_t* async, zx_status_t status,
                                          const zx_packet_signal_t* signal) {
    ZX_DEBUG_ASSERT(status == ZX_OK);
    ZX_DEBUG_ASSERT(signal->observed & ZX_EVENT_SIGNALED);

    // Clear the signal otherwise we'll keep getting called.
    // Clear the signal *before* invoking the callback because there's no
    // synchronization between the engine and the observers, thus it's possible
    // that an observer could get back to back notifications.
    event_.signal(ZX_EVENT_SIGNALED, 0u);

    // Invoke the callback.
    callback_();

    // Tell engine we're done.
    trace_notify_observer_updated(event_.get());

    return ASYNC_WAIT_AGAIN;
}

} // namespace trace

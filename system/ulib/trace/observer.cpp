// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace/observer.h>

#include <zircon/assert.h>

namespace trace {

TraceObserver::TraceObserver() {
}

TraceObserver::~TraceObserver() {
    Stop();
}

void TraceObserver::Start(async_dispatcher_t* dispatcher, fbl::Closure callback) {
    ZX_DEBUG_ASSERT(dispatcher);
    ZX_DEBUG_ASSERT(callback);

    Stop();
    callback_ = fbl::move(callback);

    zx_status_t status = zx::event::create(0u, &event_);
    ZX_ASSERT(status == ZX_OK);
    trace_register_observer(event_.get());

    wait_.set_object(event_.get());
    wait_.set_trigger(ZX_EVENT_SIGNALED);
    BeginWait(dispatcher);
}

void TraceObserver::Stop() {
    wait_.Cancel();
    callback_ = nullptr;

    if (event_) {
        trace_unregister_observer(event_.get());
        event_.reset();
    }
}

void TraceObserver::Handle(async_dispatcher_t* dispatcher, async::WaitBase* wait, zx_status_t status,
                           const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
        Stop();
        return;
    }

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

    // Wait again!
    BeginWait(dispatcher);
}

void TraceObserver::BeginWait(async_dispatcher_t* dispatcher) {
    zx_status_t status = wait_.Begin(dispatcher);
    if (status != ZX_OK)
        Stop();
}

} // namespace trace

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace/observer.h>

#include <magenta/assert.h>

namespace trace {

TraceObserver::TraceObserver() {
    wait_.set_handler(fbl::BindMember(this, &TraceObserver::Handle));
}

TraceObserver::~TraceObserver() {
    Stop();
}

void TraceObserver::Start(async_t* async, fbl::Closure callback) {
    MX_DEBUG_ASSERT(async);
    MX_DEBUG_ASSERT(callback);

    Stop();
    async_ = async;
    callback_ = fbl::move(callback);

    mx_status_t status = mx::event::create(0u, &event_);
    MX_ASSERT(status == MX_OK);

    wait_.set_object(event_.get());
    wait_.set_trigger(MX_EVENT_SIGNALED);
    status = wait_.Begin(async_);
    MX_DEBUG_ASSERT(status == MX_OK);

    trace_register_observer(event_.get());
}

void TraceObserver::Stop() {
    if (!async_)
        return;

    trace_unregister_observer(event_.get());

    mx_status_t status = wait_.Cancel(async_);
    MX_DEBUG_ASSERT(status == MX_OK);

    async_ = nullptr;
    callback_ = nullptr;
}

async_wait_result_t TraceObserver::Handle(async_t* async, mx_status_t status,
                                          const mx_packet_signal_t* signal) {
    MX_DEBUG_ASSERT(status == MX_OK);
    MX_DEBUG_ASSERT(signal->observed & MX_EVENT_SIGNALED);

    // Clear the signal before invoking the callback.
    event_.signal(MX_EVENT_SIGNALED, 0u);

    // Invoke the callback.
    callback_();
    return ASYNC_WAIT_AGAIN;
}

} // namespace trace

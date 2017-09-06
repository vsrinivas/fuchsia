// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <async/wait.h>

namespace async {

Wait::Wait(mx_handle_t object, mx_signals_t trigger, uint32_t flags)
    : async_wait_t{{ASYNC_STATE_INIT}, &Wait::CallHandler, object, trigger, flags, {}} {}

Wait::~Wait() = default;

mx_status_t Wait::Begin(async_t* async) {
    return async_begin_wait(async, this);
}

mx_status_t Wait::Cancel(async_t* async) {
    return async_cancel_wait(async, this);
}

async_wait_result_t Wait::CallHandler(async_t* async, async_wait_t* wait,
                                      mx_status_t status, const mx_packet_signal_t* signal) {
    return static_cast<Wait*>(wait)->handler_(async, status, signal);
}

} // namespace async

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//
// Helper for implementing trace observers in C++.
//

#pragma once

#include <trace-engine/instrumentation.h>

#include <async/wait.h>
#include <mx/event.h>
#include <mxtl/function.h>

#ifdef __cplusplus

namespace trace {

// Receives notifications when the trace state or set of enabled categories changes.
class TraceObserver {
public:
    // Initializes the trace observer.
    TraceObserver();

    // Stops watching for state changes and destroys the observer.
    ~TraceObserver();

    // Starts watching for state changes.
    //
    // |async| the asynchronous dispatcher, must not be null.
    // |callback| the callback which is invoked whenever a state change is observed.
    void Start(async_t* async, mxtl::Closure callback);

    // Stops watching for state changes.
    void Stop();

private:
    async_wait_result_t Handle(async_t* async, mx_status_t status,
                               const mx_packet_signal_t* signal);

    async_t* async_ = nullptr;
    mxtl::Closure callback_;
    mx::event event_;
    async::Wait wait_;
};

} // namespace trace

#endif // __cplusplus

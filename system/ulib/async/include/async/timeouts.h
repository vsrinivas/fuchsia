// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <async/async.h>

#ifdef __cplusplus

namespace async {

// C++ wrapper for a pending wait operation with an associated timeout.
// This object must not be destroyed until the wait has completed or been
// successfully canceled or the dispatcher itself has been destroyed.
// A separate instance must be used for each wait.
//
// Use |MX_TIME_INFINITE| as the deadline to wait indefinitely.
//
// Warning: This helper will only work correctly with non-concurrent dispatchers.
class WaitWithTimeout : private async_wait_t, private async_task_t {
public:
    WaitWithTimeout();
    explicit WaitWithTimeout(mx_handle_t object, mx_signals_t trigger,
                             mx_time_t deadline = MX_TIME_INFINITE,
                             uint32_t flags = 0u);
    virtual ~WaitWithTimeout();

    mx_handle_t object() const { return async_wait_t::object; }
    void set_object(mx_handle_t object) { async_wait_t::object = object; }

    mx_signals_t trigger() const { return async_wait_t::trigger; }
    void set_trigger(mx_signals_t trigger) { async_wait_t::trigger = trigger; }

    mx_time_t deadline() const { return async_task_t::deadline; }
    void set_deadline(mx_time_t deadline) { async_task_t::deadline = deadline; }

    uint32_t flags() const { return async_wait_t::flags; }
    void set_flags(uint32_t flags) { async_wait_t::flags = flags; }

    mx_status_t Begin(async_t* async);
    mx_status_t Cancel(async_t* async);

    // Note: The task's flags are managed internally by this object so they are not
    // exposed to the client.

    // Override this method to handle the wait or timeout results.
    // Reports the |status| of the wait.  If the status is |MX_OK| then |signal|
    // describes the signal which was received, otherwise |signal| is null.
    //
    // Timeouts are indicated with status |MX_ERR_TIMED_OUT|.
    //
    // The result indicates whether the wait should be repeated; it may
    // modify the wait's properties before returning.
    //
    // The result must be |ASYNC_WAIT_FINISHED| if |status| was not |MX_OK|.
    virtual async_wait_result_t Handle(async_t* async, mx_status_t status,
                                       const mx_packet_signal_t* signal) = 0;

private:
    static async_wait_result_t WaitHandler(async_t* async, async_wait_t* wait,
                                           mx_status_t status,
                                           const mx_packet_signal_t* signal);
    static async_task_result_t TimeoutHandler(async_t* async, async_task_t* task,
                                              mx_status_t status);

    ASYNC_DISALLOW_COPY_ASSIGN_AND_MOVE(WaitWithTimeout);
};

} // namespace async

#endif // __cplusplus

// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <err.h>

#include <kernel/event.h>
#include <sys/types.h>

// "Event" for use by StateTracker. This should be waited on from only a single thread, but may be
// signaled from many threads (Signal() is thread-safe).
class WaitEvent {
public:
    WaitEvent() {
        event_init(&event_, false, 0u);
    }
    ~WaitEvent() {
        event_destroy(&event_);
    }

    WaitEvent(const WaitEvent&) = delete;
    WaitEvent& operator=(const WaitEvent&) = delete;

    // Returns:
    // NO_ERROR - signaled
    // ERR_TIMED_OUT - time out expired
    // ERR_INTERRUPTED - thread killed
    status_t Wait(lk_time_t timeout) {
        return event_wait_timeout(&event_, timeout, true);
    }

    // returns number of ready threads
    int Signal() {
        return event_signal_etc(&event_, false, NO_ERROR);
    }

private:
    event_t event_;
};

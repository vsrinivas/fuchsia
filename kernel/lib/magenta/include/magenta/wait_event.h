// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <err.h>

#include <kernel/event.h>
#include <mxtl/canary.h>
#include <sys/types.h>

// "Event" for use by StateTracker. This should be waited on from only a single thread, but may be
// signaled from many threads (Signal() is thread-safe).
class WaitEvent {
public:
    WaitEvent(uint32_t opts = 0) {
        event_init(&event_, false, opts);
    }
    ~WaitEvent() {
        event_destroy(&event_);
    }

    WaitEvent(const WaitEvent&) = delete;
    WaitEvent& operator=(const WaitEvent&) = delete;

    // Returns:
    // MX_OK - signaled
    // MX_ERR_TIMED_OUT - time out expired
    // ERR_INTERRUPTED - thread killed
    // Or the |status| which the caller specified in WaitEvent::Signal(status)
    status_t Wait(lk_time_t deadline) {
        return event_wait_deadline(&event_, deadline, true);
    }

    // returns number of ready threads
    int Signal(status_t status = MX_OK) {
        return event_signal_etc(&event_, false, status);
    }

    status_t Unsignal() {
        return event_unsignal(&event_);
    }

private:
    mxtl::Canary<mxtl::magic("WEVT")> canary_;
    event_t event_;
};

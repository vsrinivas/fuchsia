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
    enum class Result : status_t {
        // Internal-use only:
        INVALID = ERR_INTERNAL,
        // Returned by Wait(), but not valid for Signal():
        INTERRUPTED = ERR_INTERRUPTED,
        TIMED_OUT = ERR_TIMED_OUT,
        // Valid for Signal():
        CANCELLED = ERR_HANDLE_CLOSED,
        SATISFIED = NO_ERROR,
        UNSATISFIABLE = ERR_BAD_STATE,
    };

    // Converts a Result to an appropriate |status_t|.
    inline static status_t ResultToStatus(Result result) { return static_cast<status_t>(result); }

    // Returns true if a return value to wait has an associated context.
    static bool HaveContextForResult(Result result);

    WaitEvent();
    WaitEvent(const WaitEvent&) = delete;
    WaitEvent& operator=(const WaitEvent&) = delete;

    ~WaitEvent();

    // If |context| is non-null and HaveContextForResult(return value) is true, then |*context| will
    // be set to the context passed to the first call to Signal().
    Result Wait(lk_time_t timeout, uint64_t* context);

    // |result| must not be Result::{INVALID, INTERRUPTED, TIMED_OUT}.
    bool Signal(Result result, uint64_t context);

private:
    // 1 if Signal() has been called, 0 if not. This is an |int| since we use atomic_...() with it.
    int signaled_;

    // The result and context passed to the first call to Signal(), respectively.
    Result result_;
    uint64_t context_;

    event_t event_;
};

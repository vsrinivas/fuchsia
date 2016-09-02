// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/wait_event.h>

#include <assert.h>

#include <arch/ops.h>

// static
bool WaitEvent::HaveContextForResult(Result result) {
    switch (result) {
        case Result::INVALID:
        case Result::INTERRUPTED:
        case Result::TIMED_OUT:
            return false;
        default:
            return true;
    }
}

WaitEvent::WaitEvent()
    : signaled_(0), result_(Result::INVALID), context_(0u) {
    event_init(&event_, false, 0u);
}

WaitEvent::~WaitEvent() {
    event_destroy(&event_);
}

WaitEvent::Result WaitEvent::Wait(lk_time_t timeout, uint64_t* context) {
    status_t status = event_wait_timeout(&event_, timeout, true);
    if (status == ERR_INTERRUPTED)
        return Result::INTERRUPTED;
    if (status == ERR_TIMED_OUT)
        return Result::TIMED_OUT;
    DEBUG_ASSERT(status == NO_ERROR);

    DEBUG_ASSERT(atomic_load(&signaled_));
    DEBUG_ASSERT(result_ != Result::INVALID);
    DEBUG_ASSERT(HaveContextForResult(result_));
    if (context)
        *context = context_;
    return result_;
}

bool WaitEvent::Signal(WaitEvent::Result result, uint64_t context) {
    DEBUG_ASSERT(result != Result::INVALID && result != Result::INTERRUPTED &&
                 result != Result::TIMED_OUT);

    int expected = 0;
    if (!atomic_cmpxchg(&signaled_, &expected, 1))
        return false;  // Signal() already called!

    result_ = result;
    context_ = context;

    // WARNING: Never request a reschedule when signaling this event.  It is
    // possible that this WaitEvent is being signaled from IRQ context.
    return !!event_signal_etc(&event_, false, NO_ERROR);
}

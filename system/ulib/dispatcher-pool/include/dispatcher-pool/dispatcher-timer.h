// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/types.h>
#include <fbl/ref_ptr.h>

#include <dispatcher-pool/dispatcher-event-source.h>

namespace dispatcher {

// class Timer
//
// Timer is one of the EventSources in the dispatcher framework used to manage
// a zircon timer object.
//
// :: Handler ::
//
// Timer defines a single handler (ProcessHandler) which runs when the timer
// fires.  Returning an error from the process handler will cause the timer to
// automatically become deactivated.
//
// :: Activtation ::
//
// Activation simply requires a user to provide a valid ExecutionDomain and a
// valid ProcessHandler.  The timer kernel object itself will be allocated
// internally.
//
// :: Arming/Canceling ::
//
// Arming the timer to fire at a specific point in time and canceling the timer
// are operations protected by an internal lock, they may be called from any
// thread.  Attempting to Arm a deactivated timer will result in an error.  The
// time at which a timer fires is always an absolute time given on the
// ZX_CLOCK_MONOTONIC timeline.
//
// After firing, a timer is always disarmed.  It may be armed, canceled and
// re-armed again any number of times from the dispatch operation within the
// context of the ExecutionDomain the timer was bound to.
//
class Timer : public EventSource {
public:
    static constexpr size_t MAX_HANDLER_CAPTURE_SIZE = sizeof(void*) * 2;
    using ProcessHandler =
        fbl::InlineFunction<zx_status_t(Timer*), MAX_HANDLER_CAPTURE_SIZE>;

    static fbl::RefPtr<Timer> Create(zx_time_t early_slop_nsec = 0);

    // Activate a timer object, creating the kernel timer and binding the Timer
    // object to an execution domain and a processing handler.
    //
    // The operation will fail if the Timer has already been bound, or either
    // the domain reference or processing handler is invalid.
    zx_status_t Activate(fbl::RefPtr<ExecutionDomain> domain,
                         ProcessHandler process_handler);
    virtual void Deactivate() __TA_EXCLUDES(obj_lock_) override;
    zx_status_t Arm(zx_time_t deadline);
    void Cancel();

protected:
    void Dispatch(ExecutionDomain* domain) __TA_EXCLUDES(obj_lock_) override;

private:
    friend class fbl::RefPtr<Timer>;

    Timer(zx_time_t early_slop_nsec)
        : EventSource(ZX_TIMER_SIGNALED),
          early_slop_nsec_(early_slop_nsec) { }

    void DisarmLocked() __TA_REQUIRES(obj_lock_);
    zx_status_t SetTimerAndWaitLocked() __TA_REQUIRES(obj_lock_);

    const zx_time_t early_slop_nsec_;
    bool armed_ __TA_GUARDED(obj_lock_) = false;
    bool timer_set_ __TA_GUARDED(obj_lock_) = false;
    zx_time_t deadline_ __TA_GUARDED(obj_lock_);

    ProcessHandler process_handler_;
};

}  // namespace dispatcher

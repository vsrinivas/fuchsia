// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/timer_dispatcher.h>

#include <assert.h>
#include <err.h>
#include <platform.h>

#include <kernel/thread.h>

#include <fbl/alloc_checker.h>
#include <fbl/auto_lock.h>
#include <zircon/compiler.h>
#include <zircon/rights.h>
#include <zircon/types.h>

static void timer_irq_callback(timer* timer, zx_time_t now, void* arg) {
    // We are in IRQ context and cannot touch the timer state_tracker, so we
    // schedule a DPC to do so. TODO(cpu): figure out ways to reduce the lag.
    dpc_queue(reinterpret_cast<dpc_t*>(arg), true);
}

static void dpc_callback(dpc_t* d) {
    reinterpret_cast<TimerDispatcher*>(d->arg)->OnTimerFired();
}

zx_status_t TimerDispatcher::Create(uint32_t options,
                                    fbl::RefPtr<Dispatcher>* dispatcher,
                                    zx_rights_t* rights) {
    if (options > ZX_TIMER_SLACK_LATE)
        return ZX_ERR_INVALID_ARGS;

    slack_mode slack_mode;

    switch (options) {
    case ZX_TIMER_SLACK_CENTER: slack_mode = TIMER_SLACK_CENTER;
        break;
    case ZX_TIMER_SLACK_EARLY: slack_mode = TIMER_SLACK_EARLY;
        break;
    case ZX_TIMER_SLACK_LATE: slack_mode = TIMER_SLACK_LATE;
        break;
    default:
        return ZX_ERR_INVALID_ARGS;
    };

    fbl::AllocChecker ac;
    auto disp = new (&ac) TimerDispatcher(slack_mode);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    *rights = ZX_DEFAULT_TIMERS_RIGHTS;
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return ZX_OK;
}

TimerDispatcher::TimerDispatcher(slack_mode slack_mode)
    : slack_mode_(slack_mode),
      timer_dpc_({LIST_INITIAL_CLEARED_VALUE, &dpc_callback, this}),
      deadline_(0u), slack_(0u), cancel_pending_(false),
      timer_(TIMER_INITIAL_VALUE(timer_)) {
}

TimerDispatcher::~TimerDispatcher() {
    DEBUG_ASSERT(deadline_ == 0u);
}

void TimerDispatcher::on_zero_handles() {
    // The timers can be kept alive indefinitely by the callbacks, so
    // we need to cancel when there are no more user-mode clients.
    Guard<fbl::Mutex> guard{get_lock()};

    // We must ensure that the timer callback (running in interrupt context,
    // possibly on a different CPU) has completed before possibly destroy
    // the timer.  So cancel the timer if we haven't already.
    if (!CancelTimerLocked())
        timer_cancel(&timer_);
}

zx_status_t TimerDispatcher::Set(zx_time_t deadline, zx_duration_t slack) {
    canary_.Assert();

    Guard<fbl::Mutex> guard{get_lock()};

    bool did_cancel = CancelTimerLocked();

    // If the timer is already due, then we can set the signal immediately without
    // starting the timer.
    if ((deadline == 0u) || (deadline <= current_time())) {
        UpdateStateLocked(0u, ZX_TIMER_SIGNALED);
        return ZX_OK;
    }

    deadline_ = deadline;
    slack_ = slack;

    // If we're imminently awaiting a timer callback due to a prior cancelation request,
    // let the callback take care of restarting the timer too so everthing happens in the
    // right sequence.
    if (cancel_pending_)
        return ZX_OK;

    // We need to ref-up because the timer and the dpc don't understand
    // refcounted objects. The Release() is called either in OnTimerFired()
    // or in the complicated cancelation path above.
    AddRef();

    // We must ensure that the timer callback (running in interrupt context,
    // possibly on a different CPU) has completed before set try to set the
    // timer again.  So cancel the timer if we haven't already.
    SetTimerLocked(!did_cancel);

    return ZX_OK;
}

zx_status_t TimerDispatcher::Cancel() {
    canary_.Assert();
    Guard<fbl::Mutex> guard{get_lock()};
    CancelTimerLocked();
    return ZX_OK;
}

void TimerDispatcher::SetTimerLocked(bool cancel_first) {
    if (cancel_first)
        timer_cancel(&timer_);
    timer_set(&timer_, deadline_, slack_mode_, slack_,
        &timer_irq_callback, &timer_dpc_);
}

bool TimerDispatcher::CancelTimerLocked() {
    // Always clear the signal bit.
    UpdateStateLocked(ZX_TIMER_SIGNALED, 0u);

    // If the timer isn't pending then we're done.
    if (!deadline_)
        return false; // didn't call timer_cancel
    deadline_ = 0u;
    slack_ = 0;

    // If we're already waiting for the timer to be canceled, then we don't need
    // to cancel it again.
    if (cancel_pending_)
        return false; // didn't call timer_cancel

    // The timer is active and needs to be canceled.
    // Refcount is at least 2 because there is a pending timer that we need to cancel.
    bool timer_canceled = timer_cancel(&timer_);
    if (timer_canceled) {
        // Managed to cancel before OnTimerFired() ran. So we need to decrement the
        // ref count here.
        ASSERT(!Release());
    } else {
        // The DPC thread is about to run the callback! Yet we are holding the lock.
        // We'll let the timer callback take care of cleanup.
        cancel_pending_ = true;
    }
    return true; // did call timer_cancel
}

void TimerDispatcher::OnTimerFired() {
    canary_.Assert();

    {
        Guard<fbl::Mutex> guard{get_lock()};

        if (cancel_pending_) {
            // We previously attempted to cancel the timer but the dpc had already
            // been queued.  Suppress handling of this callback but take care to
            // restart the timer if its deadline was set in the meantime.
            cancel_pending_ = false;
            if (deadline_ != 0u) {
                // We must ensure that the timer callback (running in interrupt context,
                // possibly on a different CPU) has completed before set try to set the
                // timer again.
                SetTimerLocked(true  /* cancel first*/);
                return;
            }
        } else {
            // The timer is firing.
            UpdateStateLocked(0u, ZX_TIMER_SIGNALED);
            deadline_ = 0u;
        }
    }

    // Drop the RefCounted reference that was added in Set(). If this was the
    // last reference, the RefCounted contract requires that we delete
    // ourselves.
    if (Release())
        delete this;
}

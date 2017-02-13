// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/timer_dispatcher.h>

#include <assert.h>
#include <err.h>
#include <platform.h>

#include <kernel/auto_lock.h>
#include <kernel/thread.h>

#include <magenta/compiler.h>
#include <mxalloc/new.h>

constexpr mx_rights_t kDefaultTimersRights =
    MX_RIGHT_DUPLICATE | MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE;


static handler_return timer_irq_callback(timer* timer, lk_time_t now, void* arg) {
    // We are in IRQ context and cannot touch the timer state_tracker, so we
    // schedule a DPC to do so. TODO(cpu): figure out ways to reduce the lag.
    dpc_queue(reinterpret_cast<dpc_t*>(arg), false);
    return INT_RESCHEDULE;
}

static void dpc_callback(dpc_t* d) {
    reinterpret_cast<TimerDispatcher*>(d->arg)->OnTimerFired();
}

mx_status_t TimerDispatcher::Create(uint32_t options,
                                    mxtl::RefPtr<Dispatcher>* dispatcher,
                                    mx_rights_t* rights) {
    AllocChecker ac;
    auto disp = new (&ac) TimerDispatcher(options);
    if (!ac.check())
        return ERR_NO_MEMORY;

    *rights = kDefaultTimersRights;
    *dispatcher = mxtl::AdoptRef<Dispatcher>(disp);
    return NO_ERROR;
}

TimerDispatcher::TimerDispatcher(uint32_t /*options*/)
    : timer_dpc_({LIST_INITIAL_CLEARED_VALUE, &dpc_callback, this}),
      active_(false),
      timer_(TIMER_INITIAL_VALUE(timer_)) {
}

TimerDispatcher::~TimerDispatcher() {
    DEBUG_ASSERT(!active_);
}

mx_status_t TimerDispatcher::SetOneShot(lk_time_t deadline) {
    canary_.Assert();
    AutoLock al(&lock_);

    if (active_) {
        // Refcount is at least 2 because there is a pending timer that we need to cancel.
        if (timer_cancel(&timer_) || dpc_cancel(&timer_dpc_)) {
            // Managed to cancel before OnTimerFired() ran. So we need to decrement the
            // ref count here.
            ASSERT(!Release());
        } else {
            // The DPC thread is about to run the callback! Yet we are
            // holding the lock. We need to let the callback finish.
            while (active_) {
                lock_.Release();
                thread_reschedule();
                lock_.Acquire();
            }
        }
    }

    state_tracker_.UpdateState(MX_TIMER_SIGNALED, 0u);

    // If |deadline| is zero it the timer is being canceled.
    if (deadline == 0u)
        return NO_ERROR;

    // We need to ref-up because the timer and the dpc don't understand
    // refcounted objects. The Release() is called either in OnTimerFired()
    // or in the complicated cancelation path above.
    AddRef();
    active_ = true;
    timer_set_oneshot(&timer_, deadline, &timer_irq_callback, &timer_dpc_);
    return NO_ERROR;
}

mx_status_t TimerDispatcher::CancelOneShot() {
    return SetOneShot(0u);
}

void TimerDispatcher::OnTimerFired() {
    canary_.Assert();
    {
        AutoLock al(&lock_);
        active_ = false;
        state_tracker_.UpdateState(0u, MX_TIMER_SIGNALED);
    }
    // Unlike SetOneShot(), this could be the last reference so we might need
    // to destroy ourselves. In Magenta, the 'delete' is called by the holder
    // of the object, not inside Release().
    if (Release())
        delete this;
}

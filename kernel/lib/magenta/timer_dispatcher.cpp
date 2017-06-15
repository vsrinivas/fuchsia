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
#include <magenta/rights.h>
#include <mxalloc/new.h>

#include <safeint/safe_math.h>

constexpr mx_duration_t kMinTimerPeriod = MX_TIMER_MIN_PERIOD;
constexpr mx_time_t     kMinTimerDeadline = MX_TIMER_MIN_DEADLINE;
constexpr mx_duration_t kTimerCanceled = 1u;

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
        return MX_ERR_NO_MEMORY;

    *rights = MX_DEFAULT_TIMERS_RIGHTS;
    *dispatcher = mxtl::AdoptRef<Dispatcher>(disp);
    return MX_OK;
}

TimerDispatcher::TimerDispatcher(uint32_t /*options*/)
    : timer_dpc_({LIST_INITIAL_CLEARED_VALUE, &dpc_callback, this}),
      deadline_(0u), period_(0u),
      timer_(TIMER_INITIAL_VALUE(timer_)) {
}

TimerDispatcher::~TimerDispatcher() {
    DEBUG_ASSERT(deadline_ == 0u);
}

void TimerDispatcher::on_zero_handles() {
    // The timers can be kept alive indefinitely by the callbacks, so
    // we need to cancel when there are no more user-mode clients.
    Cancel();
}

mx_status_t TimerDispatcher::Set(mx_time_t deadline, mx_duration_t period) {
    canary_.Assert();

    // Deadline values 0 and 1 are special.
    if (deadline < kMinTimerDeadline)
        return MX_ERR_INVALID_ARGS;

    // zero period is valid but other small values are not.
    if ((period < kMinTimerPeriod) && (period != 0u))
        return MX_ERR_NOT_SUPPORTED;

    AutoLock al(&lock_);

    CancelLocked();

    // The timer is always a one shot timer which in the periodic case
    // is re-issued in the timer callback.
    deadline_ = deadline;
    period_ = period;

    // We need to ref-up because the timer and the dpc don't understand
    // refcounted objects. The Release() is called either in OnTimerFired()
    // or in the complicated cancelation path above.
    AddRef();
    timer_set_oneshot(&timer_, deadline_, &timer_irq_callback, &timer_dpc_);
    return MX_OK;
}

mx_status_t TimerDispatcher::Cancel() {
    canary_.Assert();
    AutoLock al(&lock_);
    CancelLocked();
    return MX_OK;
}

void TimerDispatcher::CancelLocked() {
    if (deadline_) {
        // The timer is active and needs to be canceled.
        // Refcount is at least 2 because there is a pending timer that we need to cancel.
        if (timer_cancel(&timer_) || dpc_cancel(&timer_dpc_)) {
            // Managed to cancel before OnTimerFired() ran. So we need to decrement the
            // ref count here.
            ASSERT(!Release());
        } else {
            // The DPC thread is about to run the callback! Yet we are
            // holding the lock. We need to let the callback finish.
            //
            // The protocol is to zero both period_ and deadline_ members
            // and wait for deadline to be == 1. The timer callback will
            // call Release().
            period_ = 0u;
            deadline_ = 0u;

            while (deadline_ != kTimerCanceled) {
                lock_.Release();
                thread_reschedule();
                lock_.Acquire();
            }
        }

        deadline_ = 0u;
    }

    state_tracker_.UpdateState(MX_TIMER_SIGNALED, 0u);
}

void TimerDispatcher::OnTimerFired() {
    canary_.Assert();

    {
        AutoLock al(&lock_);

        if ((period_ == 0u) && (deadline_ == 0u)) {
            // The timer is being canceled. Follow the handshake protocol
            // which requires to set deadline_ and call Release().
            deadline_ = kTimerCanceled;
        } else if (period_ != 0) {
            // The timer is a periodic timer. Re-issue the timer and
            // don't Release() the reference.
            state_tracker_.StrobeState(MX_TIMER_SIGNALED);

            // Compute the next deadline while guarding for integer overflows.
            safeint::CheckedNumeric<mx_time_t> next_deadline(deadline_);
            next_deadline += period_;
            deadline_ = next_deadline.ValueOrDefault(0u);
            if (deadline_ == 0u)
                return;

            timer_set_oneshot(&timer_, deadline_, &timer_irq_callback, &timer_dpc_);
            return;
        } else {
            // The timer is a one-shot timer.
            state_tracker_.UpdateState(0u, MX_TIMER_SIGNALED);
            deadline_ = 0u;
        }
    }
    // This could be the last reference so we might need to destroy ourselves.
    // In Magenta RefPtrs, the 'delete' is called by the holder of the object.
    if (Release())
        delete this;
}

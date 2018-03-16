// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <lib/zx/timer.h>

#include <dispatcher-pool/dispatcher-execution-domain.h>
#include <dispatcher-pool/dispatcher-timer.h>

namespace dispatcher {

// static
fbl::RefPtr<Timer> Timer::Create(zx_time_t early_slop_nsec) {
    fbl::AllocChecker ac;

    auto ptr = new (&ac) Timer(early_slop_nsec);
    if (!ac.check())
        return nullptr;

    return fbl::AdoptRef(ptr);
}

zx_status_t Timer::Activate(fbl::RefPtr<ExecutionDomain> domain,
                            ProcessHandler process_handler) {
    if (process_handler == nullptr)
        return ZX_ERR_INVALID_ARGS;

    fbl::AutoLock obj_lock(&obj_lock_);
    if (is_active() || handle_.is_valid())
        return ZX_ERR_BAD_STATE;

    zx::timer timer;
    zx_status_t res = zx::timer::create(0, ZX_CLOCK_MONOTONIC, &timer);
    if (res != ZX_OK)
        return res;

    // TODO(johngro): Set the early slop time on the timer.

    res = ActivateLocked(fbl::move(timer), fbl::move(domain));
    if (res != ZX_OK)
        return res;

    process_handler_ = fbl::move(process_handler);

    return ZX_OK;
}

void Timer::Deactivate() {
    ProcessHandler old_process_handler;

    {
        fbl::AutoLock obj_lock(&obj_lock_);
        DisarmLocked();
        InternalDeactivateLocked();

        // If we are in the process of actively dispatching, do not discard our
        // handler just yet.  It is currently being used by the dispatch thread.
        // Instead, wait until the dispatch thread unwinds and allow it to clean
        // up the handler.
        //
        // Otherwise, transfer the handler state into local storage and let it
        // destruct after we have released the object lock.
        if (dispatch_state() != DispatchState::Dispatching) {
            ZX_DEBUG_ASSERT((dispatch_state() == DispatchState::Idle) ||
                            (dispatch_state() == DispatchState::WaitingOnPort));
            old_process_handler = fbl::move(process_handler_);
        }
    }
}

zx_status_t Timer::Arm(zx_time_t deadline) {
    fbl::AutoLock obj_lock(&obj_lock_);

    // If we are in the process of waiting on the port, or there is a dispatch
    // in flight, attempt to cancel the pending timer operation.
    if ((dispatch_state() == DispatchState::WaitingOnPort) ||
        (dispatch_state() == DispatchState::DispatchPending)) {
        CancelPendingLocked();
    }

    // Reset the armed state of the timer.
    DisarmLocked();

    // If we are no longer active, we cannot arm the timer.
    if (!is_active())
        return ZX_ERR_BAD_HANDLE;

    // If we are still active, we should still have a valid handle
    ZX_DEBUG_ASSERT(handle_.is_valid());

    // Record the current armed status.
    armed_ = true;
    deadline_ = deadline;

    // If we are currently Idle, set the timer and post a wait on our port.
    // Otherwise, there is a dispatch in flight that we failed to cancel.  The
    // timer will take appropriate action when the in-flight operation hits
    // Dispatch.
    if (dispatch_state() == DispatchState::Idle) {
        return SetTimerAndWaitLocked();
    }

    return ZX_OK;
}

void Timer::Cancel() {
    fbl::AutoLock obj_lock(&obj_lock_);

    // Disarm the timer and clear the internal bookkeeping.
    DisarmLocked();

    // If the timer handle has been closed, or the timer object is in the idle
    // state, or we are in the middle of a dispatch, then we are done.
    if (!handle_.is_valid() ||
       (dispatch_state() == DispatchState::Idle) ||
       (dispatch_state() == DispatchState::Dispatching)) {
        return;
    }

    // We are either waiting on the port, or we are waiting in the dispatch
    // queue.  Attemtp to cancel any pending dispatch operation.  It's OK if
    // this fails, it just means that the dispatch operation is in flight; we'll
    // figure it out by the time we hit Dispatch.
    ZX_DEBUG_ASSERT((dispatch_state() == DispatchState::WaitingOnPort) ||
                    (dispatch_state() == DispatchState::DispatchPending));
    if (dispatch_state() == DispatchState::WaitingOnPort)
        CancelPendingLocked();
}

void Timer::Dispatch(ExecutionDomain* domain) {
    ZX_DEBUG_ASSERT(domain != nullptr);
    ZX_DEBUG_ASSERT(process_handler_ != nullptr);

    // Check to make sure this timer should still fire.  It is possible that the
    // timer was canceled or changed at a point where the dispatch operation was
    // already in flight and could not be cancelled.
    bool do_dispatch;
    {
        fbl::AutoLock obj_lock(&obj_lock_);
        ZX_DEBUG_ASSERT(dispatch_state() == DispatchState::Dispatching);
        timer_set_ = false;

        // If we were disarmed, we are now back in the idle state
        if (!armed_) {
            dispatch_state_ = DispatchState::Idle;
            return;
        }

        // If the timer was moved into the future, skip the dispatch operation,
        // but fall into the code which re-sets the timer.  Otherwise, the timer
        // has now fired and we should reset our internal bookkeeping.
        do_dispatch = ((zx_clock_get(ZX_CLOCK_MONOTONIC) + early_slop_nsec_) >= deadline_);
        if (do_dispatch) {
            DisarmLocked();
        }
    }

    // Now, if we are actually supposed to dispatch this event, do so.
    zx_status_t res = do_dispatch ? process_handler_(this) : ZX_OK;

    // Finally, handle either re-arming the timer, or cleaning up as needed.
    ProcessHandler old_process_handler;
    {
        fbl::AutoLock obj_lock(&obj_lock_);
        ZX_DEBUG_ASSERT(dispatch_state() == DispatchState::Dispatching);
        dispatch_state_ = DispatchState::Idle;

        // Was there a problem during processing?  If so, make sure that we
        // de-activate ourselves.  Otherwise, if we are still active, and we are
        // supposed to be armed, attempt to set up the next wait-on-port
        // operation.
        if (res != ZX_OK) {
            InternalDeactivateLocked();
        } else {
            if (is_active() && armed_) {
                res = SetTimerAndWaitLocked();
                if (res != ZX_OK) {
                    // TODO(johngro) : This should not fail, we should probably
                    // log something instead of simply silently deactivating.
                    InternalDeactivateLocked();
                }
            }
        }

        // Have we become deactivated (either during dispatching or just now)?
        // If so, move our process handler state outside of our lock so that it
        // can safely destruct.
        if (!is_active()) {
            old_process_handler = fbl::move(process_handler_);
        }
    }
}

void Timer::DisarmLocked() {
    // If the timer was set at the kernel level, cancel it.  No matter what, we
    // are now no longer armed.
    if (timer_set_ && handle_.is_valid()) {
        zx_timer_cancel(handle_.get());
    }
    timer_set_ = false;
    armed_ = false;
}

zx_status_t Timer::SetTimerAndWaitLocked() {
    ZX_DEBUG_ASSERT(armed_);

    zx_status_t res = zx_timer_set(handle_.get(), deadline_, 0);
    if (res != ZX_OK) {
        DisarmLocked();
        return res;
    }
    timer_set_ = true;

    res = WaitOnPortLocked();
    if (res != ZX_OK) {
        DisarmLocked();
    }

    return res;
}

}  // namespace dispatcher

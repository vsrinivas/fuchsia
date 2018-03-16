// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <lib/zx/timer.h>

#include <dispatcher-pool/dispatcher-execution-domain.h>
#include <dispatcher-pool/dispatcher-wakeup-event.h>

namespace dispatcher {

// static
fbl::RefPtr<WakeupEvent> WakeupEvent::Create() {
    fbl::AllocChecker ac;

    auto ptr = new (&ac) WakeupEvent();
    if (!ac.check())
        return nullptr;

    return fbl::AdoptRef(ptr);
}

zx_status_t WakeupEvent::Activate(fbl::RefPtr<ExecutionDomain> domain,
                                  ProcessHandler process_handler) {
    if (process_handler == nullptr)
        return ZX_ERR_INVALID_ARGS;

    fbl::AutoLock obj_lock(&obj_lock_);
    zx::event event;
    zx_status_t res = zx::event::create(0, &event);
    if (res != ZX_OK)
        return res;

    res = ActivateLocked(fbl::move(event), fbl::move(domain));
    if (res != ZX_OK)
        return res;

    res = WaitOnPortLocked();
    if (res != ZX_OK) {
        InternalDeactivateLocked();
        return res;
    }

    process_handler_ = fbl::move(process_handler);

    return ZX_OK;
}

void WakeupEvent::Deactivate() {
    ProcessHandler old_process_handler;

    {
        fbl::AutoLock obj_lock(&obj_lock_);
        InternalDeactivateLocked();

        // If we were previously signalled, we are not any more.
        signaled_ = false;

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

zx_status_t WakeupEvent::Signal() {
    fbl::AutoLock obj_lock(&obj_lock_);


    // If we are no longer active, we cannot signal the event.
    if (!is_active())
        return ZX_ERR_BAD_HANDLE;

    // If we are still active, then our handle had better be valid.
    ZX_DEBUG_ASSERT(handle_.is_valid());

    // Update our internal bookkeeping.
    signaled_ = true;

    // If we have already fired and are in the process of dispatching, don't
    // bother to actually signal the event at the kernel level.
    if ((dispatch_state() == DispatchState::DispatchPending) ||
        (dispatch_state() == DispatchState::Dispatching)) {
        return ZX_OK;
    }

    zx_status_t res = zx_object_signal(handle_.get(), 0u, ZX_USER_SIGNAL_0);
    ZX_DEBUG_ASSERT(res == ZX_OK);  // I cannot think of any reason that this should ever fail.

    return res;
}

void WakeupEvent::Dispatch(ExecutionDomain* domain) {
    ZX_DEBUG_ASSERT(domain != nullptr);
    ZX_DEBUG_ASSERT(process_handler_ != nullptr);

    {
        // Clear the signalled flag.  Someone might signal us again during the
        // dispatch operation.
        fbl::AutoLock obj_lock(&obj_lock_);
        ZX_DEBUG_ASSERT(dispatch_state() == DispatchState::Dispatching);
        signaled_ = false;
    }

    zx_status_t res = process_handler_(this);
    ProcessHandler old_process_handler;
    {
        fbl::AutoLock obj_lock(&obj_lock_);
        ZX_DEBUG_ASSERT(dispatch_state() == DispatchState::Dispatching);
        dispatch_state_ = DispatchState::Idle;

        // Was there a problem during processing?  If so, make sure that we
        // de-activate ourselves.
        if (res != ZX_OK) {
            InternalDeactivateLocked();
        }

        // Are we still active?  If so, either setup the next port wait
        // operation, or re-queue ourselves if we were signalled during the
        // dispatch operation.
        if (is_active()) {
            if (signaled_) {
                dispatch_state_ = DispatchState::WaitingOnPort;
                res = domain->AddPendingWork(this);
            } else {
                res = zx_object_signal(handle_.get(), ZX_USER_SIGNAL_0, 0u);
                if (res == ZX_OK)
                    res = WaitOnPortLocked();
            }

            if (res != ZX_OK) {
                dispatch_state_ = DispatchState::Idle;
                InternalDeactivateLocked();
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

}  // namespace dispatcher

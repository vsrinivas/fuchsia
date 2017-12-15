// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <fbl/auto_call.h>

#include <dispatcher-pool/dispatcher-event-source.h>
#include <dispatcher-pool/dispatcher-execution-domain.h>
#include <dispatcher-pool/dispatcher-thread-pool.h>

namespace dispatcher {

EventSource::EventSource(zx_signals_t process_signal_mask)
    : process_signal_mask_(process_signal_mask) { }

EventSource::~EventSource() {
    ZX_DEBUG_ASSERT(domain_ == nullptr);
    ZX_DEBUG_ASSERT(!InExecutionDomain());
    ZX_DEBUG_ASSERT(!InPendingList());
    ZX_DEBUG_ASSERT(dispatch_state() == DispatchState::Idle);
}

void EventSource::InternalDeactivateLocked() {
    // If we are no longer active, we can just get out now.  We should be able
    // to assert that our handle has been closed and that we are in either the
    // Idle or Dispatching state, or that handle is still valid and we are in
    // WaitingOnPort state (meaning that there is a thread in flight from the
    // thread pool which is about to realize that we have become deactivated)
    if (!is_active()) {
        ZX_DEBUG_ASSERT(
                ( handle_.is_valid() &&  (dispatch_state() == DispatchState::WaitingOnPort)) ||
                (!handle_.is_valid() && ((dispatch_state() == DispatchState::Dispatching) ||
                                         (dispatch_state() == DispatchState::Idle))));
        return;
    }

    // Attempt to cancel any pending operations.  Do not close the handle if it
    // was too late to cancel and we are still waiting on the port.
    CancelPendingLocked();
    if (dispatch_state() != DispatchState::WaitingOnPort) {
        ZX_DEBUG_ASSERT((dispatch_state() == DispatchState::Idle) ||
                        (dispatch_state() == DispatchState::Dispatching));
        handle_.reset();
    }

    // If we still have a domain, remove ourselves from the domain's event
    // source list, then release our reference to it.
    if (domain_ != nullptr) {
        domain_->RemoveEventSource(this);
        domain_ = nullptr;
    }

    // Release our cached thread pool reference.
    thread_pool_.reset();
}

zx_status_t EventSource::ActivateLocked(zx::handle handle, fbl::RefPtr<ExecutionDomain> domain) {
    if ((domain == nullptr) || !handle.is_valid())
        return ZX_ERR_INVALID_ARGS;

    if (is_active() || handle_.is_valid())
        return ZX_ERR_BAD_STATE;
    ZX_DEBUG_ASSERT(thread_pool_ == nullptr);

    auto thread_pool = domain->GetThreadPool();
    if (thread_pool == nullptr)
        return ZX_ERR_BAD_STATE;

    // Add ourselves to our domain's list of event sources.
    zx_status_t res = domain->AddEventSource(fbl::WrapRefPtr(this));
    if (res != ZX_OK)
        return res;

    handle_ = fbl::move(handle);
    domain_ = fbl::move(domain);
    thread_pool_ = fbl::move(thread_pool);

    return ZX_OK;
}

zx_status_t EventSource::WaitOnPortLocked() {
    // If we are attempting to wait, we should not already have a wait pending.
    // In particular, we need to be in the idle state.
    ZX_DEBUG_ASSERT(dispatch_state() == DispatchState::Idle);

    // Attempting to wait when our domain is null indicates that we are in the
    // process of dying, and the wait should be denied.
    if (!is_active())
        return ZX_ERR_BAD_STATE;
    ZX_DEBUG_ASSERT(thread_pool_ != nullptr);

    zx_status_t res = thread_pool_->WaitOnPort(handle_,
                                               reinterpret_cast<uint64_t>(this),
                                               process_signal_mask(),
                                               ZX_WAIT_ASYNC_ONCE);

    // If the wait async succeeded, then we now have a pending wait operation,
    // and the kernel is now holding an unmanaged reference to us.  Flag the
    // pending wait, and manually bump our ref count.
    if (res == ZX_OK) {
        dispatch_state_ = DispatchState::WaitingOnPort;
        this->AddRef();
    }

    return res;
}

zx_status_t EventSource::CancelPendingLocked() {
    // If we are still active, remove ourselves from the domain's
    // pending work list.
    if (is_active()) {
        // If we were on the pending work list, then our state must have been
        // DispatchPending (and now should be Idle)
        if (domain_->RemovePendingWork(this)) {
            ZX_DEBUG_ASSERT(dispatch_state() == DispatchState::DispatchPending);
            dispatch_state_ = DispatchState::Idle;
        }

        // If there is a wait operation currently pending, attempt to cancel it.
        //
        // If we succeed, manually drop the unmanaged reference which the kernel
        // was holding and transition to the Idle state.
        //
        // If we fail, it must be because the wait has completed and is being
        // dispatched on another thread.  Do not transition to Idle, or release
        // the kernel reference.
        if (dispatch_state() == DispatchState::WaitingOnPort) {
            ZX_DEBUG_ASSERT(thread_pool_ != nullptr);
            zx_status_t res =
                thread_pool_->CancelWaitOnPort(handle_, reinterpret_cast<uint64_t>(this));

            if (res == ZX_OK) {
                __UNUSED bool should_destruct;

                dispatch_state_ = DispatchState::Idle;
                should_destruct = this->Release();

                ZX_DEBUG_ASSERT(should_destruct == false);
            } else {
                ZX_DEBUG_ASSERT(res == ZX_ERR_NOT_FOUND);
            }
        }
    }

    return (dispatch_state() == DispatchState::Idle) ? ZX_OK : ZX_ERR_BAD_STATE;
}

bool EventSource::BeginDispatching() {
    fbl::AutoLock obj_lock(&obj_lock_);
    if (dispatch_state() != DispatchState::DispatchPending)
        return false;

    ZX_DEBUG_ASSERT(InPendingList());

    __UNUSED zx_status_t res;
    res = CancelPendingLocked();
    ZX_DEBUG_ASSERT(res == ZX_OK);
    ZX_DEBUG_ASSERT(dispatch_state() == DispatchState::Idle);

    dispatch_state_ = DispatchState::Dispatching;

    return true;
}

fbl::RefPtr<ExecutionDomain> EventSource::ScheduleDispatch(
        const zx_port_packet_t& pkt) {
    // Something interesting happened.  Enter the lock and...
    //
    // 1) Sanity check, then reset wait_pending_.  There is no longer a wait pending.
    // 2) Assert that something interesting happened.  If none of the
    //    interesting things which happened are in the process_signal_mask_, then
    //    just return.  The dispatcher thread will deactive us.
    // 3) If our domain is still active, add this event source to the pending work
    //    queue.  If we are the first event source to enter the queue, return a
    //    reference to our domain to the dispatcher thread so that it can start
    //    to process the pending work.
    fbl::AutoLock obj_lock(&obj_lock_);

    ZX_DEBUG_ASSERT(dispatch_state() == DispatchState::WaitingOnPort);
    ZX_DEBUG_ASSERT(pkt.signal.observed & process_signal_mask());

    if (domain_ == nullptr) {
        dispatch_state_ = DispatchState::Idle;
        return nullptr;
    }

    // Copy the pending port packet to the internal event source storage,
    // then add ourselves to the domain's pending queue.  If we were the
    // first event source to join our domain's pending queue, then take a
    // reference to our domain so that we can start to process pending work
    // once we have left the obj_lock.  If we are not the first to join the
    // queue, just get out.  The thread which is currently handling pending
    // jobs for this domain will handle this one when the time comes.
    pending_pkt_ = pkt;
    return domain_->AddPendingWork(this) ? domain_ : nullptr;
}

}  // namespace dispatcher

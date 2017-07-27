// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <fbl/auto_call.h>

#include "drivers/audio/dispatcher-pool/dispatcher-event-source.h"
#include "drivers/audio/dispatcher-pool/dispatcher-thread.h"

namespace audio {

DispatcherEventSource::DispatcherEventSource(zx_signals_t process_signal_mask,
                                             zx_signals_t shutdown_signal_mask,
                                             uintptr_t owner_ctx)
    : client_thread_active_(DispatcherThread::AddClient() == ZX_OK),
      process_signal_mask_(process_signal_mask),
      shutdown_signal_mask_(shutdown_signal_mask),
      owner_ctx_(owner_ctx) {
}

DispatcherEventSource::~DispatcherEventSource() {
    if (client_thread_active_)
        DispatcherThread::RemoveClient();

    ZX_DEBUG_ASSERT(owner_ == nullptr);
    ZX_DEBUG_ASSERT(!InOwnersList());
}

void DispatcherEventSource::Deactivate(bool do_notify) {
    fbl::RefPtr<Owner> old_owner;

    {
        fbl::AutoLock obj_lock(&obj_lock_);
        old_owner = DeactivateLocked();
    }

    if (do_notify && (old_owner != nullptr))
        NotifyDeactivated(old_owner);
}

fbl::RefPtr<DispatcherEventSource::Owner> DispatcherEventSource::DeactivateLocked() {
    // If our handle has been closed, then we must have already been
    // deactivated.  We can fast-abort, and should also be able to assert
    // that...
    //
    // 1) There is no pending wait operation.
    // 2) We have no owner.
    //
    if (!handle_.is_valid()) {
        ZX_DEBUG_ASSERT(owner_ == nullptr);
        ZX_DEBUG_ASSERT(!wait_pending_);
        return nullptr;

    }

    // If we still have an owner, remove ourselves from the owner's event
    // source list.
    if (owner_ != nullptr) {
        owner_->RemoveEventSource(this);
    }

    // If there is a wait operation currently pending, attempt to cancel it.
    //
    // If we succeed, manually drop the unmanaged reference which the
    // kernel was holding, clear the wait_pending_ flag, and close the handle.
    //
    // If we fail, it must be because the wait has completed and is being
    // dispatched on another thread.  Do not close our handle, clear our
    // wait_pending flag, or release the kernel reference.  This will happen
    // naturally when the other thread reclaims the reference from the
    // kernel, and attempts to process the wakeup cause.
    if (wait_pending_) {
        zx_status_t res;
        res = DispatcherThread::port().cancel(handle_.get(), reinterpret_cast<uint64_t>(this));

        if (res == ZX_OK) {
            __UNUSED bool should_destruct;

            wait_pending_ = false;
            should_destruct = this->Release();

            ZX_DEBUG_ASSERT(should_destruct == false);
        } else {
            ZX_DEBUG_ASSERT(res == ZX_ERR_NOT_FOUND);
        }
    }

    if (!wait_pending_) {
        handle_.reset();
    }

    // Return our reference the owner we may have once had to the caller.  The
    // user-facing Deactivate call will use it to notify the owner that this
    // event source has become deactivated.
    return fbl::move(owner_);
}

zx_status_t DispatcherEventSource::Process(const zx_port_packet_t& pkt) {
    // Something interesting happened.  Enter the lock and...
    //
    // 1) Sanity check, then reset wait_pending_.  There is no longer a wait pending.
    // 2) Assert that something interesting happened.  If none of the
    //    interesting things which happened are in the process_signal_mask_,
    //    abort with an indication that we are shutting down.
    // 3) Take a reference to our owner, if they still exist.  If we have no
    //    owner, then we are in the process of dying.  Return an error so that
    //    we are not re-queued to our port.
    //
    fbl::RefPtr<Owner> owner;
    {
        fbl::AutoLock obj_lock(&obj_lock_);

        ZX_DEBUG_ASSERT(wait_pending_);
        wait_pending_ = false;

        ZX_DEBUG_ASSERT(pkt.signal.observed & (process_signal_mask() | shutdown_signal_mask()));
        if (!(pkt.signal.observed & process_signal_mask()))
            return ZX_ERR_CANCELED;

        if (owner_ == nullptr)
            return ZX_ERR_BAD_STATE;

        owner = owner_;
    }

    return ProcessInternal(owner, pkt);
}

zx_status_t DispatcherEventSource::WaitOnPortLocked(const zx::port& port) {
    // If we are attempting to wait, we should not already have a wait pending.
    ZX_DEBUG_ASSERT(!wait_pending_);

    // Attempting to wait when our owner is null indicates that we are in the
    // process of dying, and the wait should be denied.
    if (owner_ == nullptr)
        return ZX_ERR_BAD_STATE;

    zx_status_t res = handle_.wait_async(DispatcherThread::port(),
                                         reinterpret_cast<uint64_t>(this),
                                         process_signal_mask() | shutdown_signal_mask(),
                                         ZX_WAIT_ASYNC_ONCE);

    // If the wait async succeeded, then we now have a pending wait operation,
    // and the kernel is now holding an unmanaged reference to us.  Flag the
    // pending wait, and manually bump our ref count.
    if (res == ZX_OK) {
        wait_pending_ = true;
        this->AddRef();
    }

    return res;
}

void DispatcherEventSource::Owner::ShutdownDispatcherEventSources() {
    // Flag ourselves as deactivated.  This will prevent any new event sources
    // from being added to the sources_ list.  We can then swap the contents of
    // the sources_ list with a temp list, leave the lock and deactivate all of
    // the sources at our leisure.
    fbl::DoublyLinkedList<fbl::RefPtr<DispatcherEventSource>> to_deactivate;

    {
        fbl::AutoLock activation_lock(&sources_lock_);
        if (deactivated_) {
            ZX_DEBUG_ASSERT(sources_.is_empty());
            return;
        }

        deactivated_ = true;
        to_deactivate.swap(sources_);
    }

    // Now deactivate all of our event sources and release all of our references.
    for (auto& source : to_deactivate) {
        source.Deactivate(true);
    }

    to_deactivate.clear();
}

zx_status_t DispatcherEventSource::Owner::AddEventSource(
        fbl::RefPtr<DispatcherEventSource>&& event_source) {
    if (event_source == nullptr)
        return ZX_ERR_INVALID_ARGS;

    // This check is a bit sketchy...  This event_source should *never* be in any
    // Owner's event_source list at this point in time, however if it is, we don't
    // really know what lock we need to obtain to make this observation
    // atomically.  That said, the check will not mutate any state, so it should
    // be safe.  It just might not catch a bad situation which should never
    // happen.
    ZX_DEBUG_ASSERT(!event_source->InOwnersList());

    // If this Owner has become deactivated, then it is not accepting any new
    // event sources.  Fail the request to add this event_source.
    fbl::AutoLock sources_lock(&sources_lock_);
    if (deactivated_)
        return ZX_ERR_BAD_STATE;

    // We are still active.  Transfer the reference to this event_source to our set
    // of sources.
    sources_.push_front(fbl::move(event_source));
    return ZX_OK;
}

void DispatcherEventSource::Owner::RemoveEventSource(DispatcherEventSource* event_source) {
    fbl::AutoLock sources_lock(&sources_lock_);

    // Has this DispatcherEventSource::Owner become deactivated?  If so, then
    // this event_source may still be on a list (the local 'to_deactivate' list
    // in ShutdownDispatcherEventSources), but it is not in the Owner's sources_
    // list, so there is nothing to do here.
    if (deactivated_) {
        ZX_DEBUG_ASSERT(sources_.is_empty());
        return;
    }

    // If the event_source has not already been removed from the owners list, do
    // so now.
    if (event_source->InOwnersList())
        sources_.erase(*event_source);
}

}  // namespace audio

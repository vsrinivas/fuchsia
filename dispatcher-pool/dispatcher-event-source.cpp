// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <fbl/auto_call.h>

#include "drivers/audio/dispatcher-pool/dispatcher-event-source.h"
#include "drivers/audio/dispatcher-pool/dispatcher-thread.h"

namespace audio {

// Static storage
fbl::Mutex DispatcherEventSource::active_sources_lock_;
fbl::WAVLTree<uint64_t, fbl::RefPtr<DispatcherEventSource>>
    DispatcherEventSource::active_sources_;

// Translation unit local vars (hidden in an anon namespace)
namespace {
static fbl::atomic_uint64_t driver_event_source_id_gen(1u);
}

DispatcherEventSource::DispatcherEventSource(zx_signals_t process_signal_mask,
                                             zx_signals_t shutdown_signal_mask,
                                             uintptr_t owner_ctx)
    : client_thread_active_(DispatcherThread::AddClient() == ZX_OK),
      bind_id_(driver_event_source_id_gen.fetch_add(1u)),
      process_signal_mask_(process_signal_mask),
      shutdown_signal_mask_(shutdown_signal_mask),
      owner_ctx_(owner_ctx) {
}

DispatcherEventSource::~DispatcherEventSource() {
    if (client_thread_active_)
        DispatcherThread::RemoveClient();

    ZX_DEBUG_ASSERT(owner_ == nullptr);
    ZX_DEBUG_ASSERT(!InOwnersList());
    ZX_DEBUG_ASSERT(!InActiveEventSourceSet());
}

void DispatcherEventSource::Deactivate(bool do_notify) {
    fbl::RefPtr<Owner> old_owner;

    {
        fbl::AutoLock obj_lock(&obj_lock_);

        {
            fbl::AutoLock sources_lock(&active_sources_lock_);
            if (InActiveEventSourceSet()) {
                active_sources_.erase(*this);
            } else {
                // Right now, the only way to leave the active event source set
                // (once successfully Activated) is to Deactivate.  Because of
                // this, if we are not in the event source set when this is
                // triggered, we should be able to ASSERT that we have no owner,
                // and that our event source handle has been closed.
                //
                // If this assumption ever changes (eg, if there is ever a way
                // to leave the active event source set without being removed
                // from our owner's set or closing our handle), we will need to
                // come back and fix this code.
                ZX_DEBUG_ASSERT(owner_ == nullptr);
                ZX_DEBUG_ASSERT(!handle_.is_valid());
                return;
            }
        }

        if (owner_ != nullptr) {
            owner_->RemoveEventSource(this);
            old_owner = fbl::move(owner_);
        }

        handle_.reset();
    }

    if (do_notify && (old_owner != nullptr))
        NotifyDeactivated(old_owner);
}

zx_status_t DispatcherEventSource::Process(const zx_port_packet_t& port_packet) {
    // If our owner still exists, take a reference to them and call our source
    // specific process handler.
    //
    // If the owner has gone away, then we should already be in the process
    // of shutting down.  Don't bother to report an error, we are already
    // being cleaned up.
    fbl::RefPtr<Owner> owner;
    {
        fbl::AutoLock obj_lock(&obj_lock_);
        if (owner_ == nullptr)
            return ZX_OK;
        owner = owner_;
    }

    return ProcessInternal(owner, port_packet);
}

zx_status_t DispatcherEventSource::WaitOnPortLocked(const zx::port& port) {
    return handle_.wait_async(DispatcherThread::port(),
                              bind_id(),
                              process_signal_mask() | shutdown_signal_mask(),
                              ZX_WAIT_ASYNC_ONCE);
}

zx_status_t DispatcherEventSource::AddToActiveEventSources(
        fbl::RefPtr<DispatcherEventSource>&& source) {
    fbl::AutoLock sources_lock(&active_sources_lock_);

    if (!active_sources_.insert_or_find(fbl::move(source)))
        return ZX_ERR_BAD_STATE;

    return ZX_OK;
}

void DispatcherEventSource::RemoveFromActiveEventSources() {
    fbl::AutoLock sources_lock(&active_sources_lock_);
    ZX_DEBUG_ASSERT(InActiveEventSourceSet());
    active_sources_.erase(*this);
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

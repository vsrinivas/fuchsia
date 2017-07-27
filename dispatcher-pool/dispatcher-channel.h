// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <zx/channel.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/intrusive_wavl_tree.h>
#include <fbl/mutex.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <fbl/slab_allocator.h>
#include <unistd.h>

#include "drivers/audio/dispatcher-pool/dispatcher-event-source.h"

namespace audio {

class DispatcherChannel;
using DispatcherChannelAllocTraits =
    fbl::StaticSlabAllocatorTraits<fbl::RefPtr<DispatcherChannel>>;
using DispatcherChannelAllocator = fbl::SlabAllocator<DispatcherChannelAllocTraits>;

// class DispatcherChannel
//
// DispatcherChannels are objects used in the dispatcher-pool framework to
// manage a zircon channel endpoint, and its relationship to the dispatcher
// thread pool (@see DispatcherThread) and the channel endpoint's Owner (@see
// DispatcherChannel::Owner).
//
// DispatcherChannels begin their lives as empty objects.  They have neither an
// Owner, nor a channel endpoint until they become Activated.  During
// Activation, DispatcherChannels are...
//
// ## Assigned an Owner.
// ## Take ownership of a channel endpoint.
// ## Join the set of active channels being managed by the global dispatcher
//    thread pool (binding their channel endpoint to the thread pool's port in
//    the process).
//
// There are two ways to Activate a DispatcherChannel.  One may either give the
// DispatcherChannel a channel endpoint to manage, or one may have the
// Dispatcher channel create a new channel, claim one of the endpoints and give
// the other endpoint back to the caller.  In both cases, a reference to the
// channel's new owner must be supplied.  One may not Activate an already active
// channel; attempting to do so will return an error and the channel will remain
// active.
//
// Once activated, DispatcherChannels will notify their Owners via a callback
// dispatched from the global thread pool when...
//
// ## The channel has pending messages available for read
//    (DispatcherChannel::Owner::ProcessChannel)
// ## The channel has become deactivated.
//    (DispatcherChannel::Owner::NotifyChannelDeactivated)
//
// No locks are held during any of these callbacks.  A channel *may* become
// de-activated during a call to an Owner's process channel, at which point in
// time calls to channel.Read and channel.Write will begin to fail.
//
// An Owner's ProcessChannel method will never be called concurrently for the
// same specific channel, but may be called concurrently for two different
// channels owned by the same Owner.  IOW - If an Owner owns channels A and B,
// there will never be multiple call to owner.ProcessChannel(ChA, ...) in flight
// at once, but there can be a call to owner.ProcessChannel(ChA, ...) in flight
// at the same time as a call to owner.ProcessChannel(ChB, ...).
//
// TODO(johngro): Make this true.   Currently, we cannot guarantee the above
// statement with the ports V1 implementation (we will be able to once we
// transition to ports V2).
//
// There are three ways to deactivate a channel once it has become activated.
//
// ## Explicitly call the Deactivate method of the channel.
// ## Implicitly deactivate the channel by signalling any error from a call to
//    Owner::ProcessChannel
// ## Explicitly shut down all of an Owner's channels with the
//    ShutdownDispatcherChannels method of DispatcherChannel::Owner.
//
// When a channel becomes deactivated, it will (by default) notify its Owner
// with a call to DispatcherChannel::Owner::NotifyChannelDeactivated.  A
// callback will only be generated if the channel was active at the time of
// deactivation.  This behavior can be overridden by setting the 'do_notify'
// parameter of the explicit deactivation method to false, in which case no
// callback will be generated regardless of the activation state of the
// DispatcherChannel.
//
// Both of the explicit deactivation methods are safe to call from within the
// context of an owner's ProcessChannel callback, however any
// NotifyChannelDeactivated callbacks will take place within the context of the
// call to channel.Deactivate or owner.ShutdownDispatcherChannels.  Owner
// implementers who are holding synchronization primitives at the time of
// deactivation should request that no-callbacks be generated and handle cleanup
// of their internal state after deactivation has finished.
//
// TODO(johngro) : In a follow-up change, go back and add a 'do_notify'
// parameter to ShutdownDispatcherChannels.
//
// Deactivation of a channel automatically closes the underlying channel
// endpoint handle, but does not automatically synchronize with any callbacks in
// flight.  Because of this channels should not be re-activated; user should
// release their old DispatcherChannel reference and allocate a new one instead
// of attempting to re-use their DispatcherChannel objects.
//
// TODO(johngro) : In a follow-up change, just change this so that
// DispatcherChannels are one-time-activation only so that users cannot make
// this mistake.
//
class DispatcherChannel : public DispatcherEventSource,
                          public fbl::SlabAllocated<DispatcherChannelAllocTraits> {
public:
    zx_status_t Activate(fbl::RefPtr<Owner>&& owner, zx::channel* client_channel_out)
        __TA_EXCLUDES(obj_lock_);

    zx_status_t Activate(fbl::RefPtr<Owner>&& owner, zx::channel&& client_channel)
        __TA_EXCLUDES(obj_lock_) {
        fbl::AutoLock obj_lock(&obj_lock_);
        return ActivateLocked(fbl::move(owner), fbl::move(client_channel));
    }

    zx_status_t Read(void* buf,
                     uint32_t buf_len,
                     uint32_t* bytes_read_out,
                     zx::handle* rxed_handle = nullptr) const
        __TA_EXCLUDES(obj_lock_);

    zx_status_t Write(const void* buf,
                      uint32_t buf_len,
                      zx::handle&& tx_handle = zx::handle()) const
        __TA_EXCLUDES(obj_lock_);

protected:
    zx_status_t ProcessInternal(const fbl::RefPtr<Owner>& owner,
                                const zx_port_packet_t& port_packet)
        __TA_EXCLUDES(obj_lock_) override;

    void NotifyDeactivated(const fbl::RefPtr<Owner>& owner)
        __TA_EXCLUDES(obj_lock_) override;

private:
    friend DispatcherChannelAllocator;

    DispatcherChannel(uintptr_t owner_ctx = 0)
        : DispatcherEventSource(ZX_CHANNEL_READABLE,
                                ZX_CHANNEL_PEER_CLOSED,
                                owner_ctx) { }

    zx_status_t ActivateLocked(fbl::RefPtr<Owner>&& owner, zx::channel&& channel)
        __TA_REQUIRES(obj_lock_);
};

}  // namespace audio

FWD_DECL_STATIC_SLAB_ALLOCATOR(::audio::DispatcherChannelAllocTraits);


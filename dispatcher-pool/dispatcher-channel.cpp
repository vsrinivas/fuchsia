// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <magenta/syscalls.h>
#include <magenta/types.h>
#include <mxtl/auto_call.h>

#include "drivers/audio/dispatcher-pool/dispatcher-channel.h"
#include "drivers/audio/dispatcher-pool/dispatcher-thread.h"

// Instantiate storage for the static allocator.
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(::audio::DispatcherChannelAllocTraits, 0x100, true);

namespace audio {

// Static storage
mxtl::Mutex DispatcherChannel::active_channels_lock_;
mxtl::WAVLTree<uint64_t, mxtl::RefPtr<DispatcherChannel>> DispatcherChannel::active_channels_;

// Translation unit local vars (hidden in an anon namespace)
namespace {
static mxtl::atomic_uint64_t driver_channel_id_gen(1u);
}

DispatcherChannel::DispatcherChannel(uintptr_t owner_ctx)
    : client_thread_active_(DispatcherThread::AddClient() == MX_OK),
      bind_id_(driver_channel_id_gen.fetch_add(1u)),
      owner_ctx_(owner_ctx) {
}

DispatcherChannel::~DispatcherChannel() {
    if (client_thread_active_)
        DispatcherThread::RemoveClient();

    MX_DEBUG_ASSERT(owner_ == nullptr);
    MX_DEBUG_ASSERT(!InOwnersList());
    MX_DEBUG_ASSERT(!InActiveChannelSet());
}

mx_status_t DispatcherChannel::Activate(mxtl::RefPtr<Owner>&& owner,
                                        mx::channel* client_channel_out) {
    // Arg and constant state checks first
    if ((client_channel_out == nullptr) || client_channel_out->is_valid())
        return MX_ERR_INVALID_ARGS;

    if (owner == nullptr)
        return MX_ERR_INVALID_ARGS;

    // Create the channel endpoints.
    mx::channel channel;
    mx_status_t res;

    res = mx::channel::create(0u, &channel, client_channel_out);
    if (res != MX_OK)
        return res;

    // Lock and attempt to activate.
    {
        mxtl::AutoLock obj_lock(&obj_lock_);
        res = ActivateLocked(mxtl::move(owner), mxtl::move(channel));
    }
    MX_DEBUG_ASSERT(channel == MX_HANDLE_INVALID);

    // If something went wrong, make sure we close the channel endpoint we were
    // going to give back to the caller.
   if (res != MX_OK)
       client_channel_out->reset();

   return res;
}

mx_status_t DispatcherChannel::WaitOnPortLocked(const mx::port& port) {
    return channel_.wait_async(DispatcherThread::port(),
                               bind_id(),
                               MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED,
                               MX_WAIT_ASYNC_ONCE);
}

mx_status_t DispatcherChannel::ActivateLocked(mxtl::RefPtr<Owner>&& owner, mx::channel&& channel) {
    if (!channel.is_valid())
        return MX_ERR_INVALID_ARGS;

    if ((client_thread_active_ == false) ||
        (channel_ != MX_HANDLE_INVALID)  ||
        (owner_   != nullptr))
        return MX_ERR_BAD_STATE;

    // Add ourselves to the set of active channels so that users can fetch
    // references to us.
    {
        mxtl::AutoLock channels_lock(&active_channels_lock_);
        if (!active_channels_.insert_or_find(mxtl::WrapRefPtr(this)))
            return MX_ERR_BAD_STATE;

    }

    // Take ownership of the channel reference given to us.
    channel_ = mxtl::move(channel);

    // Make sure we remove ourselves from the active channel set and release our
    // channel reference if anything goes wrong.
    //
    // NOTE: This auto-call lambda needs to be flagged as not-subject to thread
    // analysis.  Currently, clang it not quite smart enough to know that since
    // the obj_lock_ is being held for the duration of ActivateLocked, and the
    // AutoCall lambda will execute during the unwind of ActicateLocked, that
    // the lock will be held during execution of the lambda.
    //
    // You can mark the lambda as __TA_REQUIRES(obj_lock_), but clang still
    // cannot seem to figure out that the lock is properly held during the
    // destruction of AutoCall object (probably because of the indirection
    // introduced by AutoCall::~AutoCall() --> Lambda().)
    //
    // For now, just disable thread analysis for this lambda.
    auto cleanup = mxtl::MakeAutoCall([this]() __TA_NO_THREAD_SAFETY_ANALYSIS {
        channel_.reset();
        mxtl::AutoLock channels_lock(&active_channels_lock_);
        MX_DEBUG_ASSERT(InActiveChannelSet());
        active_channels_.erase(*this);
    });

    // Setup our initial async wait operation on our thread pool's port.
    mx_status_t res = WaitOnPortLocked(DispatcherThread::port());
    if (res != MX_OK)
        return res;

    // Finally, add ourselves to our Owner's list of channels. Note; if this
    // operation fails, leaving the active channels set will be handle by the
    // cleanup AutoCall and canceling the async wait operation should occur as a
    // side effect of channel being auto closed as it goes out of scope.
    res = owner->AddChannel(mxtl::WrapRefPtr(this));
    if (res != MX_OK)
        return res;

    // Success, take ownership of our owner reference and cancel our
    // cleanup routine.
    owner_   = mxtl::move(owner);
    cleanup.cancel();
    return res;
}

void DispatcherChannel::Deactivate(bool do_notify) {
    mxtl::RefPtr<Owner> old_owner;

    {
        mxtl::AutoLock obj_lock(&obj_lock_);

        {
            mxtl::AutoLock channels_lock(&active_channels_lock_);
            if (InActiveChannelSet()) {
                active_channels_.erase(*this);
            } else {
                // Right now, the only way to leave the active channel set (once
                // successfully Activated) is to Deactivate.  Because of this,
                // if we are not in the channel set when this is triggered, we
                // should be able to ASSERT that we have no owner, and that our
                // channel_ handle has been closed.
                //
                // If this assumption ever changes (eg, if there is ever a way
                // to leave the active channel set without being removed from
                // our owner's set or closing our channel handle), we will need
                // to come back and fix this code.
                MX_DEBUG_ASSERT(owner_ == nullptr);
                MX_DEBUG_ASSERT(!channel_.is_valid());
                return;
            }
        }

        if (owner_ != nullptr) {
            owner_->RemoveChannel(this);
            old_owner = mxtl::move(owner_);
        }

        channel_.reset();
    }

    if (do_notify && (old_owner != nullptr))
        old_owner->NotifyChannelDeactivated(*this);
}

mx_status_t DispatcherChannel::Process(const mx_port_packet_t& port_packet) {
    mx_status_t res = MX_OK;

    // No one should be calling us if we have no messages to read.
    MX_DEBUG_ASSERT(port_packet.signal.observed & MX_CHANNEL_READABLE);
    MX_DEBUG_ASSERT(port_packet.signal.count);

    // If our owner still exists, take a reference to them and call their
    // ProcessChannel handler.
    //
    // If the owner has gone away, then we should already be in the process
    // of shutting down.  Don't bother to report an error, we are already
    // being cleaned up.
    mxtl::RefPtr<Owner> owner;
    {
        mxtl::AutoLock obj_lock(&obj_lock_);
        if (owner_ == nullptr)
            return MX_OK;
        owner = owner_;
    }

    // Process all of the pending messages in the channel before re-joining the
    // thread pool.  If our owner becomes deactivated during processing, just
    // get out early.  Don't bother to signal an error; if our owner was
    // deativated then we are in the process of shutting down already.
    //
    // TODO(johngro) : Start to establish some sort of fair scheduler-like
    // behavior.  We do not want to dominate the thread pool processing a single
    // channel for a single client.
    for (uint64_t i = 0; (i < port_packet.signal.count) && (res == MX_OK); ++i) {
        if (!owner->deactivated()) {
            res = owner->ProcessChannel(this);
        }
    }

    return res;
}

mx_status_t DispatcherChannel::Read(void*       buf,
                                    uint32_t    buf_len,
                                    uint32_t*   bytes_read_out,
                                    mx::handle* rxed_handle) const {
    if (!buf || !buf_len || !bytes_read_out ||
       ((rxed_handle != nullptr) && rxed_handle->is_valid()))
        return MX_ERR_INVALID_ARGS;

    mxtl::AutoLock obj_lock(&obj_lock_);

    uint32_t rxed_handle_count = 0;
    return channel_.read(0,
                         buf, buf_len, bytes_read_out,
                         rxed_handle ? rxed_handle->get_address() : nullptr,
                         rxed_handle ? 1 : 0,
                         &rxed_handle_count);
}

mx_status_t DispatcherChannel::Write(const void*  buf,
                                     uint32_t     buf_len,
                                     mx::handle&& tx_handle) const {
    mx_status_t res;
    if (!buf || !buf_len)
        return MX_ERR_INVALID_ARGS;

    mxtl::AutoLock obj_lock(&obj_lock_);
    if (!tx_handle.is_valid())
        return channel_.write(0, buf, buf_len, nullptr, 0);

    mx_handle_t h = tx_handle.release();
    res = channel_.write(0, buf, buf_len, &h, 1);
    if (res != MX_OK)
        tx_handle.reset(h);

    return res;
}

void DispatcherChannel::Owner::ShutdownDispatcherChannels() {
    // Flag ourselves as deactivated.  This will prevent any new channels from
    // being added to the channels_ list.  We can then swap the contents of the
    // channels_ list with a temp list, leave the lock and deactivate all of the
    // channels at our leisure.
    mxtl::DoublyLinkedList<mxtl::RefPtr<DispatcherChannel>> to_deactivate;

    {
        mxtl::AutoLock activation_lock(&channels_lock_);
        if (deactivated_) {
            MX_DEBUG_ASSERT(channels_.is_empty());
            return;
        }

        deactivated_ = true;
        to_deactivate.swap(channels_);
    }

    // Now deactivate all of our channels and release all of our references.
    for (auto& channel : to_deactivate) {
        channel.Deactivate(true);
    }

    to_deactivate.clear();
}

mx_status_t DispatcherChannel::Owner::AddChannel(mxtl::RefPtr<DispatcherChannel>&& channel) {
    if (channel == nullptr)
        return MX_ERR_INVALID_ARGS;

    // This check is a bit sketchy...  This channel should *never* be in any
    // Owner's channel list at this point in time, however if it is, we don't
    // really know what lock we need to obtain to make this observation
    // atomically.  That said, the check will not mutate any state, so it should
    // be safe.  It just might not catch a bad situation which should never
    // happen.
    MX_DEBUG_ASSERT(!channel->InOwnersList());

    // If this Owner has become deactivated, then it is not accepting any new
    // channels.  Fail the request to add this channel.
    mxtl::AutoLock channels_lock(&channels_lock_);
    if (deactivated_)
        return MX_ERR_BAD_STATE;

    // We are still active.  Transfer the reference to this channel to our set
    // of channels.
    channels_.push_front(mxtl::move(channel));
    return MX_OK;
}

void DispatcherChannel::Owner::RemoveChannel(DispatcherChannel* channel) {
    mxtl::AutoLock channels_lock(&channels_lock_);

    // Has this DispatcherChannel::Owner become deactivated?  If so, then this
    // channel may still be on a list (the local 'to_deactivate' list in
    // ShutdownDispatcherChannels), but it is not in the Owner's channels_ list, so
    // there is nothing to do here.
    if (deactivated_) {
        MX_DEBUG_ASSERT(channels_.is_empty());
        return;
    }

    // If the channel has not already been removed from the owners list, do so now.
    if (channel->InOwnersList())
        channels_.erase(*channel);
}

}  // namespace audio

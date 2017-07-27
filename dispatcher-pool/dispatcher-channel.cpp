// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <fbl/auto_call.h>

#include "drivers/audio/dispatcher-pool/dispatcher-channel.h"
#include "drivers/audio/dispatcher-pool/dispatcher-thread.h"

// Instantiate storage for the static allocator.
DECLARE_STATIC_SLAB_ALLOCATOR_STORAGE(::audio::DispatcherChannelAllocTraits, 0x100, true);

namespace audio {

zx_status_t DispatcherChannel::Activate(fbl::RefPtr<Owner>&& owner,
                                        zx::channel* client_channel_out) {
    // Arg and constant state checks first
    if ((client_channel_out == nullptr) || client_channel_out->is_valid())
        return ZX_ERR_INVALID_ARGS;

    if (owner == nullptr)
        return ZX_ERR_INVALID_ARGS;

    // Create the channel endpoints.
    zx::channel channel;
    zx_status_t res;

    res = zx::channel::create(0u, &channel, client_channel_out);
    if (res != ZX_OK)
        return res;

    // Lock and attempt to activate.
    {
        fbl::AutoLock obj_lock(&obj_lock_);
        res = ActivateLocked(fbl::move(owner), fbl::move(channel));
    }
    ZX_DEBUG_ASSERT(channel == ZX_HANDLE_INVALID);

    // If something went wrong, make sure we close the channel endpoint we were
    // going to give back to the caller.
   if (res != ZX_OK)
       client_channel_out->reset();

   return res;
}

zx_status_t DispatcherChannel::ActivateLocked(fbl::RefPtr<Owner>&& owner, zx::channel&& channel) {
    if (!channel.is_valid())
        return ZX_ERR_INVALID_ARGS;

    if ((client_thread_active() == false) ||
        (handle_  != ZX_HANDLE_INVALID)  ||
        (owner_   != nullptr))
        return ZX_ERR_BAD_STATE;

    // Take ownership of the owner and channel references given to us.
    owner_  = fbl::move(owner);
    handle_ = fbl::move(channel);

    // Make sure we deactivate ourselves if anything goes wrong.
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
    auto cleanup = fbl::MakeAutoCall([this]() __TA_NO_THREAD_SAFETY_ANALYSIS {
        DeactivateLocked();
    });

    // Setup our initial async wait operation on our thread pool's port.
    zx_status_t res = WaitOnPortLocked(DispatcherThread::port());
    if (res != ZX_OK)
        return res;

    // Finally, add ourselves to our Owner's list of event sources. Note; if this
    // operation fails, leaving the active event sources set will be handled by the
    // cleanup AutoCall and canceling the async wait operation should occur as a
    // side effect of channel being auto closed as it goes out of scope.
    res = owner_->AddEventSource(fbl::WrapRefPtr(this));
    if (res != ZX_OK)
        return res;

    // Success!  Cancel our cleanup routine.
    cleanup.cancel();
    return res;
}

void DispatcherChannel::NotifyDeactivated(const fbl::RefPtr<Owner>& owner) {
    ZX_DEBUG_ASSERT(owner != nullptr);
    owner->NotifyChannelDeactivated(*this);
}

zx_status_t DispatcherChannel::ProcessInternal(const fbl::RefPtr<Owner>& owner,
                                               const zx_port_packet_t& port_packet) {
    zx_status_t res = ZX_OK;

    // No one should be calling us if we have no messages to read.
    ZX_DEBUG_ASSERT(port_packet.signal.observed & process_signal_mask());
    ZX_DEBUG_ASSERT(port_packet.signal.count);

    // Process all of the pending messages in the channel before re-joining the
    // thread pool.  If our owner becomes deactivated during processing, just
    // get out early.  Don't bother to signal an error; if our owner was
    // deativated then we are in the process of shutting down already.
    //
    // TODO(johngro) : Start to establish some sort of fair scheduler-like
    // behavior.  We do not want to dominate the thread pool processing a single
    // channel for a single client.
    for (uint64_t i = 0; (i < port_packet.signal.count) && (res == ZX_OK); ++i) {
        if (!owner->deactivated()) {
            res = owner->ProcessChannel(this);
        }
    }

    return res;
}

zx_status_t DispatcherChannel::Read(void*       buf,
                                    uint32_t    buf_len,
                                    uint32_t*   bytes_read_out,
                                    zx::handle* rxed_handle) const {
    if (!buf || !buf_len || !bytes_read_out ||
       ((rxed_handle != nullptr) && rxed_handle->is_valid()))
        return ZX_ERR_INVALID_ARGS;

    fbl::AutoLock obj_lock(&obj_lock_);

    uint32_t rxed_handle_count = 0;
    return zx_channel_read(handle_.get(),
                           0,
                           buf,
                           rxed_handle ? rxed_handle->reset_and_get_address() : nullptr,
                           buf_len,
                           rxed_handle ? 1 : 0,
                           bytes_read_out,
                           &rxed_handle_count);
}

zx_status_t DispatcherChannel::Write(const void*  buf,
                                     uint32_t     buf_len,
                                     zx::handle&& tx_handle) const {
    zx_status_t res;
    if (!buf || !buf_len)
        return ZX_ERR_INVALID_ARGS;

    fbl::AutoLock obj_lock(&obj_lock_);
    if (!tx_handle.is_valid())
        return zx_channel_write(handle_.get(), 0, buf, buf_len, nullptr, 0);

    zx_handle_t h = tx_handle.release();
    res = zx_channel_write(handle_.get(), 0, buf, buf_len, &h, 1);
    if (res != ZX_OK)
        tx_handle.reset(h);

    return res;
}

}  // namespace audio

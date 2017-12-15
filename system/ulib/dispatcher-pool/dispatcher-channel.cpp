// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zircon/syscalls.h>
#include <zircon/types.h>
#include <fbl/auto_call.h>

#include <dispatcher-pool/dispatcher-channel.h>
#include <dispatcher-pool/dispatcher-event-source.h>
#include <dispatcher-pool/dispatcher-execution-domain.h>
#include <dispatcher-pool/dispatcher-thread-pool.h>

namespace dispatcher {

// static
fbl::RefPtr<dispatcher::Channel> dispatcher::Channel::Create() {
    fbl::AllocChecker ac;

    auto ptr = new (&ac) dispatcher::Channel();
    if (!ac.check())
        return nullptr;

    return fbl::AdoptRef(ptr);
}

zx_status_t Channel::Activate(zx::channel* client_channel_out,
                              fbl::RefPtr<ExecutionDomain> domain,
                              ProcessHandler process_handler,
                              ChannelClosedHandler channel_closed_handler) {
    // Arg and constant state checks first
    if ((client_channel_out == nullptr) || client_channel_out->is_valid())
        return ZX_ERR_INVALID_ARGS;

    if (domain == nullptr)
        return ZX_ERR_INVALID_ARGS;

    // Create the channel endpoints.
    zx::channel channel;
    zx_status_t res;

    res = zx::channel::create(0u, &channel, client_channel_out);
    if (res != ZX_OK)
        return res;

    // Attempt to activate.
    res = Activate(fbl::move(channel),
                   fbl::move(domain),
                   fbl::move(process_handler),
                   fbl::move(channel_closed_handler));

    // If something went wrong, make sure we close the channel endpoint we were
    // going to give back to the caller.
   if (res != ZX_OK)
       client_channel_out->reset();

   return res;
}

zx_status_t Channel::Activate(zx::channel channel,
                              fbl::RefPtr<ExecutionDomain> domain,
                              ProcessHandler process_handler,
                              ChannelClosedHandler channel_closed_handler) {
    // In order to activate, the supplied execution domain and channel, and
    // process handler must all be valid.  Only the deactivate handler is
    // optional.
    if ((domain == nullptr) || !channel.is_valid() || (process_handler == nullptr))
        return ZX_ERR_INVALID_ARGS;

    zx_status_t ret;
    {
        fbl::AutoLock obj_lock(&obj_lock_);
        if ((process_handler_ != nullptr) || (channel_closed_handler_ != nullptr))
            return ZX_ERR_BAD_STATE;

        ret = ActivateLocked(fbl::move(channel), fbl::move(domain));
        // If we succeeded, take control of the handlers provided by our caller.
        // Otherwise, wait until we are outside of our lock before we let the
        // handler state go out of scope and destruct.
        if (ret == ZX_OK) {
            ZX_DEBUG_ASSERT(process_handler_ == nullptr);
            ZX_DEBUG_ASSERT(channel_closed_handler_ == nullptr);
            process_handler_ = fbl::move(process_handler);
            channel_closed_handler_ = fbl::move(channel_closed_handler);
        }
    }
    return ret;
}

void Channel::Deactivate() {
    ProcessHandler       old_process_handler;
    ChannelClosedHandler old_channel_closed_handler;

    {
        fbl::AutoLock obj_lock(&obj_lock_);
        InternalDeactivateLocked();

        // If we are in the process of actively dispatching, do not discard our
        // handlers just yet.  They are currently being used by the dispatch
        // thread.  Instead, wait until the dispatch thread unwinds and allow it
        // to clean up the handlers.
        //
        // Otherwise, transfer the handler state into local storage and let them
        // destruct after we have released the object lock.
        if (dispatch_state() != DispatchState::Dispatching) {
            ZX_DEBUG_ASSERT((dispatch_state() == DispatchState::Idle) ||
                            (dispatch_state() == DispatchState::WaitingOnPort));
            old_process_handler = fbl::move(process_handler_);
            old_channel_closed_handler = fbl::move(channel_closed_handler_);
        }
    }
}

zx_status_t Channel::ActivateLocked(zx::channel channel, fbl::RefPtr<ExecutionDomain> domain) {
    ZX_DEBUG_ASSERT((domain != nullptr) && channel.is_valid());

    // Take ownership of the channel resource and execution domain reference.
    zx_status_t res = EventSource::ActivateLocked(fbl::move(channel), fbl::move(domain));
    if (res != ZX_OK) {
        return res;
    }

    // Setup our initial async wait operation on our thread pool's port.
    res = WaitOnPortLocked();
    if (res != ZX_OK) {
        InternalDeactivateLocked();
        return res;
    }

    return res;
}

void Channel::Dispatch(ExecutionDomain* domain) {
    // No one should be calling us if we have no messages to read.
    ZX_DEBUG_ASSERT(domain != nullptr);
    ZX_DEBUG_ASSERT(process_handler_ != nullptr);
    ZX_DEBUG_ASSERT(pending_pkt_.signal.observed & process_signal_mask());
    bool signal_channel_closed = (pending_pkt_.signal.observed & ZX_CHANNEL_PEER_CLOSED);

    // Do we have messages to dispatch?
    if (pending_pkt_.signal.observed & ZX_CHANNEL_READABLE) {
        // Process all of the pending messages in the channel before re-joining
        // the thread pool.
        //
        // TODO(johngro) : Start to establish some sort of fair scheduler-like
        // behavior.  We do not want to dominate the thread pool processing a
        // single channel for a single client.
        ZX_DEBUG_ASSERT(pending_pkt_.signal.count);
        for (uint64_t i = 0; i < pending_pkt_.signal.count; ++i) {
            if (domain->deactivated())
                break;

            if (process_handler_(this) != ZX_OK) {
                signal_channel_closed = true;
                break;
            }
        }
    }

    // If the other side has closed our channel, or there was an error during
    // dispatch, attempt to call our deactivate handler (if it still exists).
    if (signal_channel_closed && (channel_closed_handler_ != nullptr)) {
        channel_closed_handler_(this);
    }

    // Ok, for better or worse, dispatch is now complete.  Enter the lock and
    // deal with state transition.  If things are still healthy, attempt to wait
    // on our thread-pool's port.  If things are not healthy, go through the
    // process of deactivation.
    ProcessHandler       old_process_handler;
    ChannelClosedHandler old_channel_closed_handler;
    {
        fbl::AutoLock obj_lock(&obj_lock_);
        ZX_DEBUG_ASSERT(dispatch_state() == DispatchState::Dispatching);
        dispatch_state_ = DispatchState::Idle;

        // If we had an error during processing, or our peer closed their end of
        // the channel, make sure that we have released our handle and our
        // domain reference.
        if (signal_channel_closed) {
            InternalDeactivateLocked();
        }

        // If we are still active, attempt to set up the next wait opertaion.
        // If this fails (it should never fail) then automatically deactivate
        // ourselves.
        if (is_active()) {
            ZX_DEBUG_ASSERT(handle_.is_valid());
            zx_status_t res = WaitOnPortLocked();
            if (res != ZX_OK) {
                // TODO(johngro) : Log something about this.
                InternalDeactivateLocked();
            } else {
                ZX_DEBUG_ASSERT(dispatch_state() == DispatchState::WaitingOnPort);
            }
        }

        // If we have become deactivated for any reason, transfer our handler
        // state to local storage so that the handlers can destruct from outside
        // of our main lock.
        if (!is_active()) {
            old_process_handler = fbl::move(process_handler_);
            old_channel_closed_handler = fbl::move(channel_closed_handler_);
        }
    }
}

zx_status_t Channel::Read(void*       buf,
                          uint32_t    buf_len,
                          uint32_t*   bytes_read_out,
                          zx::handle* rxed_handle) {
    if (!buf || !buf_len || !bytes_read_out ||
       ((rxed_handle != nullptr) && rxed_handle->is_valid()))
        return ZX_ERR_INVALID_ARGS;

    fbl::AutoLock obj_lock(&obj_lock_);

    if (!handle_.is_valid())
        return ZX_ERR_BAD_HANDLE;

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

zx_status_t Channel::Write(const void*  buf,
                           uint32_t     buf_len,
                           zx::handle&& tx_handle) {
    zx_status_t res;
    if (!buf || !buf_len)
        return ZX_ERR_INVALID_ARGS;

    fbl::AutoLock obj_lock(&obj_lock_);
    if (!handle_.is_valid())
        return ZX_ERR_BAD_HANDLE;

    if (!tx_handle.is_valid())
        return zx_channel_write(handle_.get(), 0, buf, buf_len, nullptr, 0);

    zx_handle_t h = tx_handle.release();
    res = zx_channel_write(handle_.get(), 0, buf, buf_len, &h, 1);
    if (res != ZX_OK)
        tx_handle.reset(h);

    return res;
}

}  // namespace dispatcher

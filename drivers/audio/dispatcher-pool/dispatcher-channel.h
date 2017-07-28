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
namespace dispatcher {

class Channel;
using ChannelAllocTraits =
    fbl::StaticSlabAllocatorTraits<fbl::RefPtr<Channel>>;
using ChannelAllocator = fbl::SlabAllocator<ChannelAllocTraits>;

// class Channel
//
// Channel is one of the EventSources in the dispatcher framework used to manage
// a zircon channel object.
//
// :: Handlers ::
//
// Two handlers are defined for channel event sources,
// one which becomes dispatched any time the channel has messages to read (the
// ProcessHandler), and one which becomes dispatched when a channel becomes
// closed (the ChannelClosedHandler).
//
// Returning an error from the process handler will cause the channel to
// automatically become deactivated.
//
// The ChannelClosedHandler is optional, but will be called from the execution
// domain context in the event that the peer's endpoint is closed, or if an
// error is returned during processing.  It will not be called if the user's own
// code explicitly deactivates the channel, either by deactivating the channel
// object itself, or by deactivating the execution domain that the channel is
// bound to.
//
// :: Activtation ::
//
// Two forms of the Activate method are provided.  One requires that a valid
// zx::channel handle be supplied to the Channel object; this is the handle
// which will be monitored and dispatched by the Channel object.  The other form
// attempts to create a channel pair and use one half of the pair while
// returning the other half to the caller via an out-parameter, presumably so
// that they can hand this handle off to some other process which needs to
// communicate with the user.  A ProcessHandler must be provided during
// activation, a ChannelClosedHandler is optional.
//
// :: Reading/Writing ::
//
// Reading and writing on the channel is done using the Read and Write methods.
// Intenally, the handle itself is protected with a lock making it safe to call
// Read or Write on a Channel object from any thread.  Attempts to read or write
// a channel which either has not been activated yet, or which has been
// deactivated, will fail.
//
// :: A Simple Example of Activation ::
//
// class Thingy : public fbl::RefCounted<Thingy> {
//   public:
//     zx_status_t ConnectToClient(zx::channel ch_handle);
//   ...
//   private:
//     fbl::RefPtr<dispatcher::ExecutionDomain> my_domain_;
//     zx_status_t ProcessClientMessage(Channel* ch) TA_REQ(my_domain_.token());
//     void ClientDisconnected(const Channel* ch) TA_REQ(my_domain_.token());
//   ...
// };
//
// zx_status_t Thingy::ConnectToClient(zx::channel ch_handle) {
//   fbl::RefPtr<Channel> ch = Channel::Create();
//   if (!ch) return ZX_ERR_NO_MEMORY;
//
//   Channel::ProcessHandler phandler(
//   [thingy = fbl::MakeRefPtr(this)](Channel* ch) {
//      OBTAIN_EXECUTION_DOMAIN_TOKEN(t, thingy->my_domain_->token());
//      return thingy->ProcessClientMessage(ch);
//   }
//
//   Channel::ChannelCloseHandler chandler(
//   [thingy = fbl::MakeRefPtr(this)](const Channel* ch) {
//      OBTAIN_EXECUTION_DOMAIN_TOKEN(t, thingy->my_domain_->token());
//      thingy->ClientDisconnected(ch);
//   }
//
//   return ch->Activate(fbl::move(ch_handle),
//                       my_domain_,
//                       fbl::move(phandler),
//                       fbl::move(dhandler));
// }
//
class Channel : public EventSource,
                public fbl::SlabAllocated<ChannelAllocTraits> {
public:
    // Definitions of process and deactivation handlers.
    static constexpr size_t MAX_HANDLER_CAPTURE_SIZE = sizeof(void*) * 2;
    using ProcessHandler =
        fbl::InlineFunction<zx_status_t(Channel*), MAX_HANDLER_CAPTURE_SIZE>;
    using ChannelClosedHandler =
        fbl::InlineFunction<void(const Channel*), MAX_HANDLER_CAPTURE_SIZE>;

    static fbl::RefPtr<Channel> Create(uintptr_t owner_ctx = 0) {
        return ChannelAllocator::New(owner_ctx);
    }

    // Activate a channel, creating the channel pair and retuning the client's
    // channel endpoint in the process.
    //
    // This operation binds the Channel object to an ExecutionDomain, a
    // processing handler, and an optional deactivation handler.  The operation
    // will fail if the Channel has already been bound, or either the domain
    // reference or processing handler is invalid.
    zx_status_t Activate(zx::channel* client_channel_out,
                         fbl::RefPtr<ExecutionDomain> domain,
                         ProcessHandler process_handler,
                         ChannelClosedHandler channel_closed_handler = nullptr)
        __TA_EXCLUDES(obj_lock_);

    // Activate a channel, binding to the user supplied channel endpoint in the
    // process.
    zx_status_t Activate(zx::channel channel,
                         fbl::RefPtr<ExecutionDomain> domain,
                         ProcessHandler process_handler,
                         ChannelClosedHandler channel_closed_handler = nullptr)
        __TA_EXCLUDES(obj_lock_);

    void Deactivate() __TA_EXCLUDES(obj_lock_) override;

    // Depricated version of activate which register handlers that will target
    // the Owner vtable.  Clients should be switching to version of activate
    // which use the fbl::function form of registering handlers.  Once this has
    // been completed for all clients, these versions will be removed.
    zx_status_t Activate(fbl::RefPtr<Owner> owner, zx::channel* client_channel_out)
        __TA_EXCLUDES(obj_lock_);

    zx_status_t Activate(fbl::RefPtr<Owner> owner, zx::channel channel)
        __TA_EXCLUDES(obj_lock_);

    // Depricated version of deactivate
    void Deactivate(bool do_notify) { Deactivate(); }

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
    void Dispatch(ExecutionDomain* domain) __TA_EXCLUDES(obj_lock_) override;

private:
    friend ChannelAllocator;
    friend class fbl::RefPtr<Channel>;

    Channel(uintptr_t owner_ctx = 0)
        : EventSource(ZX_CHANNEL_READABLE | ZX_CHANNEL_PEER_CLOSED,
                      owner_ctx) { }

    zx_status_t ActivateLocked(zx::channel channel, fbl::RefPtr<ExecutionDomain> domain)
        __TA_REQUIRES(obj_lock_);

    ProcessHandler       process_handler_;
    ChannelClosedHandler channel_closed_handler_;
};

}  // namespace dispatcher

// TODO(johngro) : remove these when existing API clients have been updated.
using DispatcherChannel = ::audio::dispatcher::Channel;
using DispatcherChannelAllocator = audio::dispatcher::ChannelAllocator;

}  // namespace audio

FWD_DECL_STATIC_SLAB_ALLOCATOR(::audio::dispatcher::ChannelAllocTraits);


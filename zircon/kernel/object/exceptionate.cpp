// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/exceptionate.h>

#include <object/exception_dispatcher.h>
#include <object/handle.h>
#include <object/message_packet.h>
#include <object/process_dispatcher.h>
#include <object/thread_dispatcher.h>
#include <zircon/errors.h>
#include <zircon/syscalls/exception.h>

zx_status_t Exceptionate::SetChannel(fbl::RefPtr<ChannelDispatcher> channel) {
    if (!channel) {
        return ZX_ERR_INVALID_ARGS;
    }

    Guard<fbl::Mutex> guard{&lock_};

    if (channel_ && !channel_->PeerHasClosed()) {
        return ZX_ERR_ALREADY_BOUND;
    }

    // At this point we're certain that either there is no channel or it's a
    // dead channel with no peer (since channel endpoints can't re-open) so we
    // can overwrite it.
    channel_ = ktl::move(channel);

    return ZX_OK;
}

void Exceptionate::ClearChannel() {
    Guard<fbl::Mutex> guard{&lock_};
    if (channel_) {
        channel_->on_zero_handles();
        channel_.reset();
    }
}

zx_status_t Exceptionate::SendException(fbl::RefPtr<ExceptionDispatcher> exception) {
    DEBUG_ASSERT(exception);

    Guard<fbl::Mutex> guard{&lock_};

    if (!channel_) {
        return ZX_ERR_NEXT;
    }

    zx_exception_info_t info;
    info.tid = exception->thread()->get_koid();
    info.pid = exception->thread()->process()->get_koid();
    info.type = exception->exception_type();

    MessagePacketPtr message;
    zx_status_t status = MessagePacket::Create(&info, sizeof(info), 1, &message);
    if (status != ZX_OK) {
        return status;
    }

    HandleOwner exception_handle(Handle::Make(ktl::move(exception),
                                              ExceptionDispatcher::default_rights()));
    if (!exception_handle) {
        return ZX_ERR_NO_MEMORY;
    }
    message->mutable_handles()[0] = exception_handle.release();
    message->set_owns_handles(true);

    status = channel_->Write(ZX_KOID_INVALID, ktl::move(message));

    // ZX_ERR_PEER_CLOSED just indicates that there's no longer an endpoint
    // to receive exceptions, simplify things for callers by collapsing this
    // into the ZX_ERR_NEXT case since it means the same thing.
    if (status == ZX_ERR_PEER_CLOSED) {
        // No need to call on_zero_handles() here, the peer is already gone.
        channel_.reset();
        return ZX_ERR_NEXT;
    }

    return status;
}

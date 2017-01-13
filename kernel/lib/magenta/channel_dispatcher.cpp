// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/channel_dispatcher.h>

#include <string.h>

#include <assert.h>
#include <err.h>
#include <new.h>
#include <trace.h>

#include <magenta/handle.h>
#include <magenta/message_packet.h>
#include <magenta/port_client.h>

#include <mxtl/type_support.h>

#define LOCAL_TRACE 0

constexpr mx_rights_t kDefaultChannelRights = MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE;

// static
status_t ChannelDispatcher::Create(uint32_t flags,
                                   mxtl::RefPtr<Dispatcher>* dispatcher0,
                                   mxtl::RefPtr<Dispatcher>* dispatcher1,
                                   mx_rights_t* rights) {
    AllocChecker ac;
    auto ch0 = mxtl::AdoptRef(new (&ac) ChannelDispatcher((flags & ~MX_FLAG_REPLY_CHANNEL)));
    if (!ac.check())
        return ERR_NO_MEMORY;

    auto ch1 = mxtl::AdoptRef(new (&ac) ChannelDispatcher(flags));
    if (!ac.check())
        return ERR_NO_MEMORY;

    ch0->Init(ch1);
    ch1->Init(ch0);

    *rights = kDefaultChannelRights;
    *dispatcher0 = mxtl::move(ch0);
    *dispatcher1 = mxtl::move(ch1);
    return NO_ERROR;
}

ChannelDispatcher::ChannelDispatcher(uint32_t flags)
    : flags_(flags), state_tracker_(MX_CHANNEL_WRITABLE) {
}

void ChannelDispatcher::Init(mxtl::RefPtr<ChannelDispatcher> other) {
    other_ = mxtl::move(other);
    other_koid_ = other_->get_koid();
}

ChannelDispatcher::~ChannelDispatcher() {
    DEBUG_ASSERT(messages_.is_empty());
}

void ChannelDispatcher::on_zero_handles() {
    // Detach other endpoint
    mxtl::RefPtr<ChannelDispatcher> other;
    {
        AutoLock lock(&lock_);
        other = mxtl::move(other_);
    }

    // Ensure other endpoint detaches us
    if (other)
        other->OnPeerZeroHandles();

    // Now that we're mutually disconnected, discard queued messages
    // There can be no other references to us, so no lock needed
    messages_.clear();
}

void ChannelDispatcher::OnPeerZeroHandles() {
    AutoLock lock(&lock_);
    other_.reset();
    state_tracker_.UpdateState(MX_CHANNEL_WRITABLE, MX_CHANNEL_PEER_CLOSED);
    if (iopc_)
        iopc_->Signal(MX_CHANNEL_PEER_CLOSED, &lock_);
}

status_t ChannelDispatcher::Read(uint32_t* msg_size,
                                 uint32_t* msg_handle_count,
                                 mxtl::unique_ptr<MessagePacket>* msg,
                                 bool may_discard) {
    auto max_size = *msg_size;
    auto max_handle_count = *msg_handle_count;

    AutoLock lock(&lock_);

    if (messages_.is_empty())
        return other_ ? ERR_SHOULD_WAIT : ERR_REMOTE_CLOSED;

    *msg_size = messages_.front().data_size();
    *msg_handle_count = messages_.front().num_handles();
    status_t rv = NO_ERROR;
    if (*msg_size > max_size || *msg_handle_count > max_handle_count) {
        if (!may_discard)
            return ERR_BUFFER_TOO_SMALL;
        rv = ERR_BUFFER_TOO_SMALL;
    }

    *msg = messages_.pop_front();

    if (messages_.is_empty())
        state_tracker_.UpdateState(MX_CHANNEL_READABLE, 0u);

    return rv;
}

status_t ChannelDispatcher::Write(mxtl::unique_ptr<MessagePacket> msg) {
    mxtl::RefPtr<ChannelDispatcher> other;
    {
        AutoLock lock(&lock_);
        if (!other_) {
            // |msg| will be destroyed but we want to keep the handles alive since
            // the caller should put them back into the process table.
            msg->set_owns_handles(false);
            return ERR_REMOTE_CLOSED;
        }
        other = other_;
    }

    return other->WriteSelf(mxtl::move(msg));
}

status_t ChannelDispatcher::WriteSelf(mxtl::unique_ptr<MessagePacket> msg) {
    AutoLock lock(&lock_);
    auto size = msg->data_size();
    messages_.push_back(mxtl::move(msg));

    state_tracker_.UpdateState(0u, MX_CHANNEL_READABLE);
    if (iopc_)
        iopc_->Signal(MX_CHANNEL_READABLE, size, &lock_);

    return NO_ERROR;
}

status_t ChannelDispatcher::set_port_client(mxtl::unique_ptr<PortClient> client) {
    AutoLock lock(&lock_);
    if (iopc_)
        return ERR_BAD_STATE;

    if ((client->get_trigger_signals() & ~(MX_CHANNEL_READABLE | MX_CHANNEL_PEER_CLOSED)) != 0)
        return ERR_INVALID_ARGS;

    iopc_ = mxtl::move(client);

    // Replay the messages that are pending.
    for (auto& msg : messages_) {
        iopc_->Signal(MX_CHANNEL_READABLE, msg.data_size(), &lock_);
    }

    return NO_ERROR;
}

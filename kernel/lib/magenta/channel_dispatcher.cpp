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

#include <magenta/channel.h>
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
    LTRACE_ENTRY;

    AllocChecker ac;
    mxtl::RefPtr<Channel> channel = mxtl::AdoptRef(new (&ac) Channel());
    if (!ac.check()) return ERR_NO_MEMORY;

    auto msgp0 = new (&ac) ChannelDispatcher((flags & ~MX_FLAG_REPLY_PIPE), 0u, channel);
    if (!ac.check()) return ERR_NO_MEMORY;

    auto msgp1 = new (&ac) ChannelDispatcher(flags, 1u, channel);
    if (!ac.check()) {
        delete msgp0;
        return ERR_NO_MEMORY;
    }

    msgp0->set_inner_koid(msgp1->get_koid());
    msgp1->set_inner_koid(msgp0->get_koid());

    *rights = kDefaultChannelRights;
    *dispatcher0 = mxtl::AdoptRef<Dispatcher>(msgp0);
    *dispatcher1 = mxtl::AdoptRef<Dispatcher>(msgp1);
    return NO_ERROR;
}

ChannelDispatcher::ChannelDispatcher(uint32_t flags,
                                     size_t side, mxtl::RefPtr<Channel> channel)
    : side_(side), flags_(flags), channel_(mxtl::move(channel)) {
}

ChannelDispatcher::~ChannelDispatcher() {
    channel_->OnDispatcherDestruction(side_);
}

StateTracker* ChannelDispatcher::get_state_tracker() {
    return channel_->GetStateTracker(side_);
}

status_t ChannelDispatcher::Read(uint32_t* msg_size,
                                 uint32_t* msg_handle_count,
                                 mxtl::unique_ptr<MessagePacket>* msg,
                                 bool may_discard) {
    LTRACE_ENTRY;
    return channel_->Read(side_, msg_size, msg_handle_count, msg, may_discard);
}

status_t ChannelDispatcher::Write(mxtl::unique_ptr<MessagePacket> msg) {
    LTRACE_ENTRY;
    return channel_->Write(side_, mxtl::move(msg));
}

status_t ChannelDispatcher::set_port_client(mxtl::unique_ptr<PortClient> client) {
    LTRACE_ENTRY;
    return channel_->SetIOPort(side_, mxtl::move(client));
}

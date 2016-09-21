// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/message_pipe_dispatcher.h>

#include <string.h>

#include <assert.h>
#include <err.h>
#include <new.h>
#include <trace.h>

#include <magenta/handle.h>
#include <magenta/message_packet.h>
#include <magenta/message_pipe.h>
#include <magenta/io_port_client.h>

#include <mxtl/type_support.h>

#define LOCAL_TRACE 0

constexpr mx_rights_t kDefaultPipeRights = MX_RIGHT_TRANSFER | MX_RIGHT_READ | MX_RIGHT_WRITE;

// static
status_t MessagePipeDispatcher::Create(uint32_t flags,
                                       mxtl::RefPtr<Dispatcher>* dispatcher0,
                                       mxtl::RefPtr<Dispatcher>* dispatcher1,
                                       mx_rights_t* rights) {
    LTRACE_ENTRY;

    AllocChecker ac;
    mxtl::RefPtr<MessagePipe> pipe = mxtl::AdoptRef(new (&ac) MessagePipe());
    if (!ac.check()) return ERR_NO_MEMORY;

    auto msgp0 = new (&ac) MessagePipeDispatcher((flags & ~MX_FLAG_REPLY_PIPE), 0u, pipe);
    if (!ac.check()) return ERR_NO_MEMORY;

    auto msgp1 = new (&ac) MessagePipeDispatcher(flags, 1u, pipe);
    if (!ac.check()) {
        delete msgp0;
        return ERR_NO_MEMORY;
    }

    msgp0->set_inner_koid(msgp1->get_koid());
    msgp1->set_inner_koid(msgp0->get_koid());

    *rights = kDefaultPipeRights;
    *dispatcher0 = mxtl::AdoptRef<Dispatcher>(msgp0);
    *dispatcher1 = mxtl::AdoptRef<Dispatcher>(msgp1);
    return NO_ERROR;
}

MessagePipeDispatcher::MessagePipeDispatcher(uint32_t flags,
                                             size_t side, mxtl::RefPtr<MessagePipe> pipe)
    : side_(side), flags_(flags), pipe_(mxtl::move(pipe)) {
}

MessagePipeDispatcher::~MessagePipeDispatcher() {
    pipe_->OnDispatcherDestruction(side_);
}

StateTracker* MessagePipeDispatcher::get_state_tracker() {
    return pipe_->GetStateTracker(side_);
}

status_t MessagePipeDispatcher::Read(uint32_t* msg_size,
                                     uint32_t* msg_handle_count,
                                     mxtl::unique_ptr<MessagePacket>* msg) {
    LTRACE_ENTRY;
    return pipe_->Read(side_, msg_size, msg_handle_count, msg);
}

status_t MessagePipeDispatcher::Write(mxtl::Array<uint8_t> data, mxtl::Array<Handle*> handles) {
    LTRACE_ENTRY;
    AllocChecker ac;
    mxtl::unique_ptr<MessagePacket> msg(
        new (&ac) MessagePacket(mxtl::move(data), mxtl::move(handles)));
    if (!ac.check()) return ERR_NO_MEMORY;

    return pipe_->Write(side_, mxtl::move(msg));
}

status_t MessagePipeDispatcher::set_port_client(mxtl::unique_ptr<IOPortClient> client) {
    LTRACE_ENTRY;
    return pipe_->SetIOPort(side_, mxtl::move(client));
}

// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <magenta/message_pipe.h>

#include <err.h>
#include <new.h>
#include <stddef.h>

#include <kernel/auto_lock.h>

#include <magenta/handle.h>
#include <magenta/io_port_dispatcher.h>
#include <magenta/io_port_client.h>
#include <magenta/magenta.h>
#include <magenta/message_packet.h>

namespace {

size_t other_side(size_t side) {
    return side ? 0u : 1u;
}

}  // namespace

MessagePipe::MessagePipe()
    : dispatcher_alive_{true, true} {
    state_tracker_[0].set_initial_signals_state(
            mx_signals_state_t{MX_SIGNAL_WRITABLE,
                               MX_SIGNAL_READABLE | MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED});
    state_tracker_[1].set_initial_signals_state(
            mx_signals_state_t{MX_SIGNAL_WRITABLE,
                               MX_SIGNAL_READABLE | MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED});
}

MessagePipe::~MessagePipe() {
    // No need to lock. We are single threaded and will not have new requests.
    DEBUG_ASSERT(messages_[0].is_empty());
    DEBUG_ASSERT(messages_[1].is_empty());
}

void MessagePipe::OnDispatcherDestruction(size_t side) {
    auto other = other_side(side);

    MessageList messages_to_destroy;
    {
        AutoLock lock(&lock_);
        dispatcher_alive_[side] = false;
        messages_to_destroy.swap(messages_[side]);

        if (dispatcher_alive_[other]) {
            mx_signals_t other_satisfiable_clear = MX_SIGNAL_WRITABLE;
            if (messages_[other].is_empty())
                other_satisfiable_clear |= MX_SIGNAL_READABLE;
            state_tracker_[other].UpdateState(MX_SIGNAL_WRITABLE, MX_SIGNAL_PEER_CLOSED,
                                              other_satisfiable_clear, 0u);
            if (iopc_[other])
                iopc_[other]->Signal(MX_SIGNAL_PEER_CLOSED, &lock_);
        }
    }

    messages_to_destroy.clear();
}

status_t MessagePipe::Read(size_t side,
                           uint32_t* msg_size,
                           uint32_t* msg_handle_count,
                           mxtl::unique_ptr<MessagePacket>* msg) {
    auto max_size = *msg_size;
    auto max_handle_count = *msg_handle_count;
    auto other = other_side(side);

    AutoLock lock(&lock_);

    bool other_alive = dispatcher_alive_[other];

    if (messages_[side].is_empty())
        return other_alive ? ERR_BAD_STATE : ERR_REMOTE_CLOSED;

    *msg_size = static_cast<uint32_t>(messages_[side].front().data.size());
    *msg_handle_count = static_cast<uint32_t>(messages_[side].front().handles.size());
    if (*msg_size > max_size || *msg_handle_count > max_handle_count)
        return ERR_BUFFER_TOO_SMALL;

    *msg = messages_[side].pop_front();

    if (messages_[side].is_empty()) {
        state_tracker_[side].UpdateState(MX_SIGNAL_READABLE, 0u,
                                         !other_alive ? MX_SIGNAL_READABLE : 0u, 0u);
    }

    return NO_ERROR;
}

status_t MessagePipe::Write(size_t side, mxtl::unique_ptr<MessagePacket> msg) {
    auto other = other_side(side);

    AutoLock lock(&lock_);
    bool other_alive = dispatcher_alive_[other];
    if (!other_alive) {
        // |msg| will be destroyed but we want to keep the handles alive since
        // the caller should put them back into the process table.
        msg->ReturnHandles();
        return ERR_BAD_STATE;
    }

    auto size = msg->data.size();
    messages_[other].push_back(mxtl::move(msg));

    state_tracker_[other].UpdateSatisfied(0u, MX_SIGNAL_READABLE);
    if (iopc_[other])
        iopc_[other]->Signal(MX_SIGNAL_READABLE, size, &lock_);
    return NO_ERROR;
}

StateTracker* MessagePipe::GetStateTracker(size_t side) {
    return &state_tracker_[side];
}

status_t MessagePipe::SetIOPort(size_t side, mxtl::unique_ptr<IOPortClient> client) {
    AutoLock lock(&lock_);
    if (iopc_[side])
        return ERR_BAD_STATE;

    if ((client->get_trigger_signals() & ~(MX_SIGNAL_READABLE | MX_SIGNAL_PEER_CLOSED)) != 0)
        return ERR_INVALID_ARGS;

    iopc_[side] = mxtl::move(client);

    // Replay the messages that are pending.
    for (auto& msg : messages_[side]) {
        iopc_[side]->Signal(MX_SIGNAL_READABLE, msg.data.size(), &lock_);
    }

    return NO_ERROR;
}

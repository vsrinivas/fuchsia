// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <stddef.h>

#include <kernel/auto_lock.h>
#include <magenta/handle.h>
#include <magenta/magenta.h>
#include <magenta/msg_pipe.h>

namespace {

size_t other_side(size_t side) {
    return side ? 0u : 1u;
}

}  // namespace

void MessagePacket::ReturnHandles() {
    handles.reset();
}

MessagePacket::~MessagePacket() {
    for (size_t ix = 0; ix != handles.size(); ++ix) {
        DeleteHandle(handles[ix]);
    }
}

MessagePipe::MessagePipe(mx_koid_t koid)
    : koid_(koid),
      dispatcher_alive_{true, true} {
    mutex_init(&lock_);
    state_tracker_[0].set_initial_signals_state(
            mx_signals_state_t{MX_SIGNAL_WRITABLE,
                               MX_SIGNAL_READABLE | MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED});
    state_tracker_[1].set_initial_signals_state(
            mx_signals_state_t{MX_SIGNAL_WRITABLE,
                               MX_SIGNAL_READABLE | MX_SIGNAL_WRITABLE | MX_SIGNAL_PEER_CLOSED});
}

MessagePipe::~MessagePipe() {
    // No need to lock. We are single threaded and will not have new requests.
    mutex_destroy(&lock_);

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
            state_tracker_[other].UpdateState(MX_SIGNAL_PEER_CLOSED, MX_SIGNAL_WRITABLE, 0u,
                                              other_satisfiable_clear);
        }
    }

    messages_to_destroy.clear();
}

status_t MessagePipe::Read(size_t side, utils::unique_ptr<MessagePacket>* msg) {
    bool other_alive;
    auto other = other_side(side);

    {
        AutoLock lock(&lock_);
        *msg = messages_[side].pop_front();
        other_alive = dispatcher_alive_[other];

        if (messages_[side].is_empty()) {
            state_tracker_[side].UpdateState(0u, MX_SIGNAL_READABLE, 0u,
                                             !other_alive ? MX_SIGNAL_READABLE : 0u);
        }
    }

    if (*msg)
        return NO_ERROR;
    return other_alive ? ERR_BAD_STATE : ERR_CHANNEL_CLOSED;
}

status_t MessagePipe::Write(size_t side, utils::unique_ptr<MessagePacket> msg) {
    auto other = other_side(side);

    AutoLock lock(&lock_);
    bool other_alive = dispatcher_alive_[other];
    if (!other_alive) {
        // |msg| will be destroyed but we want to keep the handles alive since
        // the caller should put them back into the process table.
        msg->ReturnHandles();
        return ERR_BAD_STATE;
    }

    messages_[other].push_back(utils::move(msg));

    state_tracker_[other].UpdateSatisfied(MX_SIGNAL_READABLE, 0u);
    return NO_ERROR;
}

StateTracker* MessagePipe::GetStateTracker(size_t side) {
    return &state_tracker_[side];
}

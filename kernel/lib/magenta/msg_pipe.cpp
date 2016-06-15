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

#include <utils/list_utils.h>

namespace {
size_t other_side(size_t side) {
    return side ? 0u : 1u;
}

void clean_list(utils::DoublyLinkedList<MessagePacket>* list) {
    MessagePacket* msg;
    do {
        msg = list->pop_front();
        delete msg;
    } while (msg);
}
}

void MessagePacket::ReturnHandles() {
    handles.reset();
}

MessagePacket::~MessagePacket() {
    for (size_t ix = 0; ix != handles.size(); ++ix) {
        handles[ix]->Close();
        DeleteHandle(handles[ix]);
    }
}

MessagePipe::MessagePipe()
    : dispatcher_alive_{true, true} {
    mutex_init(&lock_);
}

MessagePipe::~MessagePipe() {
    // No need to lock. We are single threaded and will not have new requests.
    clean_list(&messages_[0]);
    clean_list(&messages_[1]);
}

void MessagePipe::OnDispatcherDestruction(size_t side) {
    bool other_alive;
    auto other = other_side(side);

    AutoLock lock(&lock_);
    dispatcher_alive_[side] = false;
    other_alive = dispatcher_alive_[other];

    if (other_alive)
        waiter_[other].Signal(MX_SIGNAL_PEER_CLOSED);
}

status_t MessagePipe::Read(size_t side, utils::unique_ptr<MessagePacket>* msg) {
    bool other_alive;
    auto other = other_side(side);

    {
        AutoLock lock(&lock_);
        msg->reset(messages_[side].pop_front());
        other_alive = dispatcher_alive_[other];

        if (messages_[side].is_empty()) {
            waiter_[side].ClearSignal(MX_SIGNAL_READABLE);
        }
    }

    if (*msg)
        return NO_ERROR;
    return other_alive ? ERR_NO_MSG : ERR_BAD_STATE;
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

    messages_[other].push_back(msg.release());

    waiter_[other].Signal(MX_SIGNAL_READABLE);
    return NO_ERROR;
}

Waiter* MessagePipe::GetWaiter(size_t side) {
    return &waiter_[side];
}

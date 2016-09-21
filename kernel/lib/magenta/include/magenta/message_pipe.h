// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stddef.h>

#include <kernel/mutex.h>

#include <magenta/state_tracker.h>
#include <magenta/syscalls-types.h>
#include <magenta/syscalls-types.h>

#include <mxtl/intrusive_double_list.h>
#include <mxtl/ref_counted.h>

class IOPortClient;
class MessagePacket;

class MessagePipe : public mxtl::RefCounted<MessagePipe> {
public:
    MessagePipe();
    ~MessagePipe();

    void OnDispatcherDestruction(size_t side);

    // |msg_size| and |msg_handle_count| are in-out parameters. As input, they specify the maximum
    // size and handle count, respectively. On NO_ERROR or ERR_BUFFER_TOO_SMALL, they specify the
    // actual size and handle count (of the returned message in the former case and of the next
    // message in the latter).
    status_t Read(size_t side,
                  uint32_t* msg_size,
                  uint32_t* msg_handle_count,
                  mxtl::unique_ptr<MessagePacket>* msg);
    status_t Write(size_t side, mxtl::unique_ptr<MessagePacket> msg);

    StateTracker* GetStateTracker(size_t side);
    status_t SetIOPort(size_t side, mxtl::unique_ptr<IOPortClient> client);

private:
    using MessageList = mxtl::DoublyLinkedList<mxtl::unique_ptr<MessagePacket>>;

    Mutex lock_;
    bool dispatcher_alive_[2];
    MessageList messages_[2];
    NonIrqStateTracker state_tracker_[2];
    mxtl::unique_ptr<IOPortClient> iopc_[2];
};

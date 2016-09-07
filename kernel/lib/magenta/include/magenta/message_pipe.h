// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <kernel/mutex.h>

#include <magenta/state_tracker.h>
#include <magenta/syscalls-types.h>

#include <mxtl/array.h>
#include <mxtl/intrusive_double_list.h>
#include <mxtl/ref_counted.h>

class Handle;
class IOPortDispatcher;
class IOPortClient;

struct MessagePacket : public mxtl::DoublyLinkedListable<mxtl::unique_ptr<MessagePacket>> {
    MessagePacket(mxtl::Array<uint8_t>&& _data,
                  mxtl::Array<Handle*>&& _handles)
        : data(mxtl::move(_data)),
          handles(mxtl::move(_handles)) { }
    ~MessagePacket();

    mxtl::Array<uint8_t> data;
    mxtl::Array<Handle*> handles;

    void ReturnHandles();
};

class MessagePipe : public mxtl::RefCounted<MessagePipe> {
public:
    MessagePipe();
    ~MessagePipe();

    void OnDispatcherDestruction(size_t side);

    status_t Read(size_t side, mxtl::unique_ptr<MessagePacket>* msg);
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

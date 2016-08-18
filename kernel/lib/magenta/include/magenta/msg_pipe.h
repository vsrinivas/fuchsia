// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <kernel/mutex.h>

#include <magenta/state_tracker.h>

#include <utils/array.h>
#include <utils/intrusive_double_list.h>
#include <utils/ref_counted.h>

class Handle;

struct MessagePacket : public utils::DoublyLinkedListable<utils::unique_ptr<MessagePacket>> {
    MessagePacket(utils::Array<uint8_t>&& _data,
                  utils::Array<Handle*>&& _handles)
        : data(utils::move(_data)),
          handles(utils::move(_handles)) { }
    ~MessagePacket();

    utils::Array<uint8_t> data;
    utils::Array<Handle*> handles;

    void ReturnHandles();
};

class MessagePipe : public utils::RefCounted<MessagePipe> {
public:
    using MessageList = utils::DoublyLinkedList<utils::unique_ptr<MessagePacket>>;
    MessagePipe(mx_koid_t koid);
    ~MessagePipe();

    mx_koid_t get_koid() const { return koid_; }

    void OnDispatcherDestruction(size_t side);

    status_t Read(size_t side, utils::unique_ptr<MessagePacket>* msg);
    status_t Write(size_t side, utils::unique_ptr<MessagePacket> msg);

    StateTracker* GetStateTracker(size_t side);

private:
    const mx_koid_t koid_;
    bool dispatcher_alive_[2];
    MessageList messages_[2];
    // This lock protects |dispatcher_alive_| and |messages_|.
    Mutex lock_;
    StateTracker state_tracker_[2];
};

// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <kernel/mutex.h>

#include <magenta/dispatcher.h>
#include <magenta/msg_pipe.h>
#include <magenta/state_tracker.h>
#include <magenta/types.h>

#include <utils/ref_counted.h>
#include <utils/unique_ptr.h>

class MessagePipeDispatcher final : public Dispatcher {
public:
    static status_t Create(uint32_t flags, utils::RefPtr<Dispatcher>* dispatcher0,
                           utils::RefPtr<Dispatcher>* dispatcher1, mx_rights_t* rights);

    ~MessagePipeDispatcher() final;
    mx_obj_type_t GetType() const final { return MX_OBJ_TYPE_MESSAGE_PIPE; }
    MessagePipeDispatcher* get_message_pipe_dispatcher() final { return this; }
    StateTracker* get_state_tracker() final;
    mx_koid_t get_inner_koid() const final { return pipe_->get_koid(); }

    bool is_reply_pipe() const { return (flags_ & MX_FLAG_REPLY_PIPE) ? true : false; }

    status_t BeginRead(uint32_t* message_size, uint32_t* handle_count);
    status_t AcceptRead(utils::Array<uint8_t>* data, utils::Array<Handle*>* handles);
    status_t Write(utils::Array<uint8_t> data, utils::Array<Handle*> handles);

private:
    MessagePipeDispatcher(uint32_t flags, size_t side, utils::RefPtr<MessagePipe> pipe);

    const size_t side_;
    const uint32_t flags_;
    utils::RefPtr<MessagePipe> pipe_;
    utils::unique_ptr<MessagePacket> pending_;
    Mutex lock_;
};

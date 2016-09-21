// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <magenta/dispatcher.h>
#include <magenta/message_pipe.h>
#include <magenta/state_tracker.h>
#include <magenta/types.h>

#include <mxtl/array.h>
#include <mxtl/ref_counted.h>
#include <mxtl/unique_ptr.h>

class IOPortClient;

class MessagePipeDispatcher final : public Dispatcher {
public:
    static status_t Create(uint32_t flags, mxtl::RefPtr<Dispatcher>* dispatcher0,
                           mxtl::RefPtr<Dispatcher>* dispatcher1, mx_rights_t* rights);

    ~MessagePipeDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_MESSAGE_PIPE; }
    StateTracker* get_state_tracker() final;
    mx_koid_t get_inner_koid() const final { return inner_koid_; }
    status_t set_port_client(mxtl::unique_ptr<IOPortClient> client) final;

    bool is_reply_pipe() const { return (flags_ & MX_FLAG_REPLY_PIPE) ? true : false; }

    void set_inner_koid(mx_koid_t koid) { inner_koid_ = koid; }
    // See MessagePipe::Read() for details.
    status_t Read(uint32_t* msg_size,
                  uint32_t* msg_handle_count,
                  mxtl::unique_ptr<MessagePacket>* msg);
    status_t Write(mxtl::Array<uint8_t> data, mxtl::Array<Handle*> handles);

private:
    MessagePipeDispatcher(uint32_t flags, size_t side, mxtl::RefPtr<MessagePipe> pipe);

    const size_t side_;
    const uint32_t flags_;
    mx_koid_t inner_koid_;

    mxtl::RefPtr<MessagePipe> pipe_;
};

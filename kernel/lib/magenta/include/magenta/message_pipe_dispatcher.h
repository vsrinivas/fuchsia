// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <kernel/mutex.h>

#include <magenta/dispatcher.h>
#include <magenta/message_pipe.h>
#include <magenta/state_tracker.h>
#include <magenta/types.h>

#include <mxtl/ref_counted.h>
#include <mxtl/unique_ptr.h>

class IOPortClient;
class IOPortDispatcher;

class MessagePipeDispatcher final : public Dispatcher {
public:
    static status_t Create(uint32_t flags, mxtl::RefPtr<Dispatcher>* dispatcher0,
                           mxtl::RefPtr<Dispatcher>* dispatcher1, mx_rights_t* rights);

    ~MessagePipeDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_MESSAGE_PIPE; }
    StateTracker* get_state_tracker() final;
    mx_koid_t get_inner_koid() const final { return pipe_->get_koid(); }

    bool is_reply_pipe() const { return (flags_ & MX_FLAG_REPLY_PIPE) ? true : false; }

    status_t BeginRead(uint32_t* message_size, uint32_t* handle_count);
    status_t AcceptRead(mxtl::Array<uint8_t>* data, mxtl::Array<Handle*>* handles);
    status_t Write(mxtl::Array<uint8_t> data, mxtl::Array<Handle*> handles);
    status_t SetIOPort(mxtl::RefPtr<IOPortDispatcher> io_port, uint64_t key, mx_signals_t signals);

private:
    MessagePipeDispatcher(uint32_t flags, size_t side, mxtl::RefPtr<MessagePipe> pipe);

    const size_t side_;
    const uint32_t flags_;
    Mutex lock_;
    mxtl::RefPtr<MessagePipe> pipe_;
    mxtl::unique_ptr<MessagePacket> pending_;
};

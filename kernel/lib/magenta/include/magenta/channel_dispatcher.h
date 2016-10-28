// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdint.h>

#include <magenta/channel.h>
#include <magenta/dispatcher.h>
#include <magenta/state_tracker.h>
#include <magenta/types.h>

#include <mxtl/ref_counted.h>
#include <mxtl/unique_ptr.h>

class PortClient;

class ChannelDispatcher final : public Dispatcher {
public:
    static status_t Create(uint32_t flags, mxtl::RefPtr<Dispatcher>* dispatcher0,
                           mxtl::RefPtr<Dispatcher>* dispatcher1, mx_rights_t* rights);

    ~ChannelDispatcher() final;
    mx_obj_type_t get_type() const final { return MX_OBJ_TYPE_CHANNEL; }
    StateTracker* get_state_tracker() final;
    mx_koid_t get_inner_koid() const final { return inner_koid_; }
    status_t set_port_client(mxtl::unique_ptr<PortClient> client) final;

    bool is_reply_channel() const {
        return (flags_ & MX_FLAG_REPLY_CHANNEL) ? true : false;
    }

    void set_inner_koid(mx_koid_t koid) { inner_koid_ = koid; }
    // See Channel::Read() for details.
    status_t Read(uint32_t* msg_size,
                  uint32_t* msg_handle_count,
                  mxtl::unique_ptr<MessagePacket>* msg,
                  bool may_disard);
    status_t Write(mxtl::unique_ptr<MessagePacket> msg);

private:
    ChannelDispatcher(uint32_t flags, size_t side, mxtl::RefPtr<Channel> channel);

    const size_t side_;
    const uint32_t flags_;
    mx_koid_t inner_koid_;

    mxtl::RefPtr<Channel> channel_;
};

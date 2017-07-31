// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>
#include <mx/object.h>

namespace mx {

class channel : public object<channel> {
public:
    static constexpr mx_obj_type_t TYPE = MX_OBJ_TYPE_CHANNEL;

    constexpr channel() = default;

    explicit channel(mx_handle_t value) : object(value) {}

    explicit channel(handle&& h) : object(h.release()) {}

    channel(channel&& other) : object(other.release()) {}

    channel& operator=(channel&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(uint32_t flags, channel* endpoint0,
                              channel* endpoint1);

    mx_status_t read(uint32_t flags, void* bytes, uint32_t num_bytes,
                     uint32_t* actual_bytes, mx_handle_t* handles,
                     uint32_t num_handles, uint32_t* actual_handles) const {
        return mx_channel_read(get(), flags, bytes, handles, num_bytes,
                               num_handles, actual_bytes, actual_handles);
    }

    mx_status_t write(uint32_t flags, const void* bytes, uint32_t num_bytes,
                      const mx_handle_t* handles, uint32_t num_handles) const {
        return mx_channel_write(get(), flags, bytes, num_bytes, handles,
                                num_handles);
    }

    mx_status_t call(uint32_t flags, mx_time_t deadline,
                     const mx_channel_call_args_t* args,
                     uint32_t* actual_bytes, uint32_t* actual_handles,
                     mx_status_t* read_status) const {
        return mx_channel_call(get(), flags, deadline, args, actual_bytes,
                               actual_handles, read_status);
    }
};

using unowned_channel = const unowned<channel>;

} // namespace mx

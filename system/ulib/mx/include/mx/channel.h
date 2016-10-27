// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>

namespace mx {

class channel : public handle<channel> {
public:
    channel() = default;

    explicit channel(handle<void>&& h) : handle(h.release()) {}

    channel(channel&& other) : handle(other.release()) {}

    channel& operator=(channel&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(uint32_t flags, channel* endpoint0, channel* endpoint1);

    mx_status_t read(uint32_t flags,
                     void* bytes, uint32_t num_bytes, uint32_t* actual_bytes,
                     mx_handle_t* handles, uint32_t num_handles, uint32_t* actual_handles) const {
        uint32_t nb = num_bytes;
        uint32_t nh = num_handles;
        mx_status_t result = mx_msgpipe_read(get(), bytes, &nb, handles, &nh, flags);
        if (actual_bytes)
            *actual_bytes = nb;
        if (actual_handles)
            *actual_handles = nh;
        return result;
    }

    mx_status_t write(uint32_t flags, const void* bytes, uint32_t num_bytes,
                      const mx_handle_t* handles, uint32_t num_handles) const {
        return mx_msgpipe_write(get(), bytes, num_bytes, handles, num_handles, flags);
    }
};

} // namespace mx

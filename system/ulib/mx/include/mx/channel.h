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
        return mx_channel_read(get(), flags, bytes, num_bytes, actual_bytes,
                               handles, num_handles, actual_handles);
    }

    mx_status_t write(uint32_t flags, const void* bytes, uint32_t num_bytes,
                      const mx_handle_t* handles, uint32_t num_handles) const {
        return mx_channel_write(get(), flags, bytes, num_bytes, handles, num_handles);
    }
};

} // namespace mx

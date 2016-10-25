// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>

namespace mx {

class msgpipe : public handle<msgpipe> {
public:
    msgpipe() = default;

    explicit msgpipe(handle<void>&& h) : handle(h.release()) {}

    msgpipe(msgpipe&& other) : handle(other.release()) {}

    msgpipe& operator=(msgpipe&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(msgpipe* endpoint0, msgpipe* endpoint1,
                              uint32_t flags);

    mx_status_t read(void* bytes, uint32_t* num_bytes, mx_handle_t* handles,
                     uint32_t* num_handles, uint32_t flags) const {
        return mx_msgpipe_read(get(), bytes, num_bytes, handles, num_handles,
                               flags);
    }

    mx_status_t write(const void* bytes, uint32_t num_bytes,
                      const mx_handle_t* handles, uint32_t num_handles,
                      uint32_t flags) const {
        return mx_msgpipe_write(get(), bytes, num_bytes, handles, num_handles,
                                flags);
    }
};

} // namespace mx

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>

namespace mx {

class socket : public handle<socket> {
public:
    socket() = default;

    explicit socket(handle<void>&& h) : handle(h.release()) {}

    socket(socket&& other) : handle(other.release()) {}

    socket& operator=(socket&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(socket* endpoint0, socket* endpoint1,
                              uint32_t flags);

    mx_ssize_t write(uint32_t flags, mx_size_t size, const void* buffer) const {
        return mx_socket_write(get(), flags, size, buffer);
    }

    mx_ssize_t read(uint32_t flags, mx_size_t size, void* buffer) const {
        return mx_socket_read(get(), flags, size, buffer);
    }
};

} // namespace mx

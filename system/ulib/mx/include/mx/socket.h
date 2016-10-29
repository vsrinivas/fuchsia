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

    static mx_status_t create(uint32_t flags, socket* endpoint0,
                              socket* endpoint1);

    mx_status_t write(uint32_t flags, const void* buffer, mx_size_t len,
                      mx_size_t* actual) const {
        return mx_socket_write(get(), flags, buffer, len, actual);
    }

    mx_status_t read(uint32_t flags, void* buffer, mx_size_t len,
                     mx_size_t* actual) const {
        return mx_socket_read(get(), flags, buffer, len, actual);
    }
};

} // namespace mx

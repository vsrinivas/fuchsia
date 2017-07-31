// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <mx/handle.h>
#include <mx/object.h>

namespace mx {

class socket : public object<socket> {
public:
    static constexpr mx_obj_type_t TYPE = MX_OBJ_TYPE_SOCKET;

    constexpr socket() = default;

    explicit socket(mx_handle_t value) : object(value) {}

    explicit socket(handle&& h) : object(h.release()) {}

    socket(socket&& other) : object(other.release()) {}

    socket& operator=(socket&& other) {
        reset(other.release());
        return *this;
    }

    static mx_status_t create(uint32_t flags, socket* endpoint0,
                              socket* endpoint1);

    mx_status_t write(uint32_t flags, const void* buffer, size_t len,
                      size_t* actual) const {
        return mx_socket_write(get(), flags, buffer, len, actual);
    }

    mx_status_t read(uint32_t flags, void* buffer, size_t len,
                     size_t* actual) const {
        return mx_socket_read(get(), flags, buffer, len, actual);
    }
};

using unowned_socket = const unowned<socket>;

} // namespace mx

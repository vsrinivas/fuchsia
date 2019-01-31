// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_ZX_SOCKET_H_
#define LIB_ZX_SOCKET_H_

#include <lib/zx/handle.h>
#include <lib/zx/object.h>

namespace zx {

class socket : public object<socket> {
public:
    static constexpr zx_obj_type_t TYPE = ZX_OBJ_TYPE_SOCKET;

    constexpr socket() = default;

    explicit socket(zx_handle_t value) : object(value) {}

    explicit socket(handle&& h) : object(h.release()) {}

    socket(socket&& other) : object(other.release()) {}

    socket& operator=(socket&& other) {
        reset(other.release());
        return *this;
    }

    static zx_status_t create(uint32_t options, socket* endpoint0,
                              socket* endpoint1);

    zx_status_t write(uint32_t options, const void* buffer, size_t len,
                      size_t* actual) const {
        return zx_socket_write(get(), options, buffer, len, actual);
    }

    zx_status_t read(uint32_t options, void* buffer, size_t len,
                     size_t* actual) const {
        return zx_socket_read(get(), options, buffer, len, actual);
    }

    zx_status_t share(socket socket_to_share) const {
        return zx_socket_share(get(), socket_to_share.release());
    }

    zx_status_t accept(socket* out_socket) const {
        // We use a temporary to handle the case where |this| and |out_socket|
        // are aliased.
        socket result;
        zx_status_t status = zx_socket_accept(get(), result.reset_and_get_address());
        out_socket->reset(result.release());
        return status;
    }

    zx_status_t shutdown(uint32_t options) const {
        return zx_socket_shutdown(get(), options);
    }
};

using unowned_socket = unowned<socket>;

} // namespace zx

#endif  // LIB_ZX_SOCKET_H_

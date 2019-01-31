// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifdef __Fuchsia__

#include <lib/fidl/transport.h>
#include <zircon/assert.h>
#include <zircon/syscalls.h>

zx_status_t fidl_socket_write_control(zx_handle_t socket, const void* buffer,
                                      size_t capacity) {
    for (;;) {
        zx_status_t status = zx_socket_write(socket, ZX_SOCKET_CONTROL, buffer,
                                             capacity, nullptr);
        if (status != ZX_ERR_SHOULD_WAIT) {
            return status;
        }

        zx_signals_t observed = ZX_SIGNAL_NONE;
        status = zx_object_wait_one(socket, ZX_SOCKET_CONTROL_WRITABLE | ZX_SOCKET_PEER_CLOSED,
                                    ZX_TIME_INFINITE, &observed);
        if (status != ZX_OK) {
            return status;
        }

        if (observed & ZX_SOCKET_PEER_CLOSED) {
            return ZX_ERR_PEER_CLOSED;
        }

        ZX_ASSERT(observed & ZX_SOCKET_CONTROL_WRITABLE);
    }
}

zx_status_t fidl_socket_read_control(zx_handle_t socket, void* buffer,
                                     size_t capacity, size_t* out_actual) {
    for (;;) {
        zx_status_t status = zx_socket_read(socket, ZX_SOCKET_CONTROL, buffer,
                                            capacity, out_actual);
        if (status != ZX_ERR_SHOULD_WAIT) {
            return status;
        }

        zx_signals_t observed = ZX_SIGNAL_NONE;
        status = zx_object_wait_one(socket, ZX_SOCKET_CONTROL_READABLE | ZX_SOCKET_PEER_CLOSED,
                                    ZX_TIME_INFINITE, &observed);
        if (status != ZX_OK) {
            return status;
        }

        if (observed & ZX_SOCKET_CONTROL_READABLE) {
            continue;
        }

        ZX_ASSERT(observed & ZX_SOCKET_PEER_CLOSED);
        return ZX_ERR_PEER_CLOSED;
    }
}

zx_status_t fidl_socket_call_control(zx_handle_t socket, const void* buffer,
                                     size_t capacity, void* out_buffer,
                                     size_t out_capacity, size_t* out_actual) {
    zx_status_t status = fidl_socket_write_control(socket, buffer, capacity);
    if (status != ZX_OK) {
        return status;
    }
    return fidl_socket_read_control(socket, out_buffer, out_capacity, out_actual);
}

#endif

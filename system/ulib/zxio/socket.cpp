// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>

static zx_status_t zxio_socket_release(zxio_t* io, zx_handle_t* out_handle) {
    zxio_socket_t* zs = reinterpret_cast<zxio_socket_t*>(io);
    zx_handle_t socket = zs->socket.socket;
    zs->socket.socket = ZX_HANDLE_INVALID;
    *out_handle = socket;
    return ZX_OK;
}

static zx_status_t zxio_socket_close(zxio_t* io) {
    zxio_socket_t* zs = reinterpret_cast<zxio_socket_t*>(io);
    return zxs_close(&zs->socket);
}

static zx_status_t zxio_socket_read(zxio_t* io, void* buffer, size_t capacity,
                                    size_t* out_actual) {
    zxio_socket_t* zs = reinterpret_cast<zxio_socket_t*>(io);
    return zxs_recv(&zs->socket, buffer, capacity, out_actual);
}

static zx_status_t zxio_socket_write(zxio_t* io, const void* buffer,
                                     size_t capacity, size_t* out_actual) {
    zxio_socket_t* zs = reinterpret_cast<zxio_socket_t*>(io);
    return zxs_send(&zs->socket, buffer, capacity, out_actual);
}

static constexpr zxio_ops_t zxio_socket_ops = []() {
    zxio_ops_t ops = zxio_default_ops;
    ops.release = zxio_socket_release;
    ops.close = zxio_socket_close;
    ops.read = zxio_socket_read;
    ops.write = zxio_socket_write;
    return ops;
}();

zx_status_t zxio_socket_init(zxio_storage_t* storage, zxs_socket_t socket) {
    zxio_socket_t* zs = reinterpret_cast<zxio_socket_t*>(storage);
    zxio_init(&zs->io, &zxio_socket_ops);
    zs->socket = socket;
    return ZX_OK;
}

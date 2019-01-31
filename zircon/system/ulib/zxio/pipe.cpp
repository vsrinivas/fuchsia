// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <string.h>
#include <sys/stat.h>
#include <zircon/syscalls.h>

static zx_status_t zxio_pipe_release(zxio_t* io, zx_handle_t* out_handle) {
    zxio_pipe_t* pipe = reinterpret_cast<zxio_pipe_t*>(io);
    zx_handle_t socket = pipe->socket;
    pipe->socket = ZX_HANDLE_INVALID;
    *out_handle = socket;
    return ZX_OK;
}

static zx_status_t zxio_pipe_close(zxio_t* io) {
    zxio_pipe_t* pipe = reinterpret_cast<zxio_pipe_t*>(io);
    zx_handle_t socket = pipe->socket;
    pipe->socket = ZX_HANDLE_INVALID;
    zx_handle_close(socket);
    return ZX_OK;
}

static zx_status_t zxio_pipe_attr_get(zxio_t* io, zxio_node_attr_t* out_attr) {
    memset(out_attr, 0, sizeof(*out_attr));
    out_attr->mode = S_IFIFO | S_IRUSR | S_IWUSR;
    return ZX_OK;
}

static void zxio_pipe_wait_begin(zxio_t* io, zxio_signals_t zxio_signals,
                                 zx_handle_t* out_handle,
                                 zx_signals_t* out_zx_signals) {
    zxio_pipe_t* pipe = reinterpret_cast<zxio_pipe_t*>(io);
    *out_handle = pipe->socket;

    zx_signals_t zx_signals = static_cast<zx_signals_t>(zxio_signals);
    if (zxio_signals & ZXIO_READ_DISABLED) {
        zx_signals |= ZX_SOCKET_PEER_CLOSED;
    }
    *out_zx_signals = zx_signals;
}

static void zxio_pipe_wait_end(zxio_t* io, zx_signals_t zx_signals,
                               zxio_signals_t* out_zxio_signals) {
    zxio_signals_t zxio_signals =
        static_cast<zxio_signals_t>(zx_signals) & ZXIO_SIGNAL_ALL;
    if (zx_signals & ZX_SOCKET_PEER_CLOSED) {
        zxio_signals |= ZXIO_READ_DISABLED;
    }
    *out_zxio_signals = zxio_signals;
}

static zx_status_t zxio_pipe_read(zxio_t* io, void* buffer, size_t capacity,
                                  size_t* out_actual) {
    zxio_pipe_t* pipe = reinterpret_cast<zxio_pipe_t*>(io);
    zx_status_t status = zx_socket_read(pipe->socket, 0, buffer, capacity,
                                        out_actual);
    // We've reached end-of-file, which is signaled by successfully reading zero
    // bytes.
    //
    // If we see |ZX_ERR_BAD_STATE|, that implies reading has been disabled for
    // this endpoint because the only other case that generates that error is
    // passing |ZX_SOCKET_CONTROL|, which we don't do above.
    if (status == ZX_ERR_PEER_CLOSED || status == ZX_ERR_BAD_STATE) {
        *out_actual = 0u;
        return ZX_OK;
    }
    return status;
}

static zx_status_t zxio_pipe_write(zxio_t* io, const void* buffer,
                                   size_t capacity, size_t* out_actual) {
    zxio_pipe_t* pipe = reinterpret_cast<zxio_pipe_t*>(io);
    return zx_socket_write(pipe->socket, 0, buffer, capacity, out_actual);
}

static constexpr zxio_ops_t zxio_pipe_ops = []() {
    zxio_ops_t ops = zxio_default_ops;
    ops.release = zxio_pipe_release;
    ops.close = zxio_pipe_close;
    ops.wait_begin = zxio_pipe_wait_begin;
    ops.wait_end = zxio_pipe_wait_end;
    ops.attr_get = zxio_pipe_attr_get;
    ops.read = zxio_pipe_read;
    ops.write = zxio_pipe_write;
    return ops;
}();

zx_status_t zxio_pipe_init(zxio_storage_t* storage, zx_handle_t socket) {
    zxio_pipe_t* pipe = reinterpret_cast<zxio_pipe_t*>(storage);
    zxio_init(&pipe->io, &zxio_pipe_ops);
    pipe->socket = socket;
    return ZX_OK;
}

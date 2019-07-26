// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <string.h>
#include <sys/stat.h>
#include <zircon/syscalls.h>

static zx_status_t zxio_pipe_close(zxio_t* io) {
  zxio_pipe_t* pipe = reinterpret_cast<zxio_pipe_t*>(io);
  pipe->socket.reset();
  return ZX_OK;
}

static zx_status_t zxio_pipe_release(zxio_t* io, zx_handle_t* out_handle) {
  zxio_pipe_t* pipe = reinterpret_cast<zxio_pipe_t*>(io);
  *out_handle = pipe->socket.release();
  return ZX_OK;
}

static zx_status_t zxio_pipe_clone(zxio_t* io, zx_handle_t* out_handle) {
  zx::object<zx::socket> out_socket;
  zx_status_t status =
      reinterpret_cast<zxio_pipe_t*>(io)->socket.duplicate(ZX_RIGHT_SAME_RIGHTS, &out_socket);
  if (status == ZX_OK) {
    *out_handle = out_socket.release();
  }
  return status;
}

static zx_status_t zxio_pipe_attr_get(zxio_t* io, zxio_node_attr_t* out_attr) {
  *out_attr = {};
  out_attr->mode = S_IFIFO | S_IRUSR | S_IWUSR;
  return ZX_OK;
}

static void zxio_pipe_wait_begin(zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                                 zx_signals_t* out_zx_signals) {
  zxio_pipe_t* pipe = reinterpret_cast<zxio_pipe_t*>(io);
  *out_handle = pipe->socket.get();

  zx_signals_t zx_signals = static_cast<zx_signals_t>(zxio_signals);
  if (zxio_signals & ZXIO_READ_DISABLED) {
    zx_signals |= ZX_SOCKET_PEER_CLOSED;
  }
  *out_zx_signals = zx_signals;
}

static void zxio_pipe_wait_end(zxio_t* io, zx_signals_t zx_signals,
                               zxio_signals_t* out_zxio_signals) {
  zxio_signals_t zxio_signals = static_cast<zxio_signals_t>(zx_signals) & ZXIO_SIGNAL_ALL;
  if (zx_signals & ZX_SOCKET_PEER_CLOSED) {
    zxio_signals |= ZXIO_READ_DISABLED;
  }
  *out_zxio_signals = zxio_signals;
}

static zx_status_t zxio_pipe_read(zxio_t* io, void* buffer, size_t capacity, size_t* out_actual) {
  zxio_pipe_t* pipe = reinterpret_cast<zxio_pipe_t*>(io);
  zx_status_t status = pipe->socket.read(0, buffer, capacity, out_actual);
  // We've reached end-of-file, which is signaled by successfully reading zero
  // bytes.
  //
  // If we see |ZX_ERR_BAD_STATE|, that implies reading has been disabled for
  // this endpoint.
  if (status == ZX_ERR_PEER_CLOSED || status == ZX_ERR_BAD_STATE) {
    *out_actual = 0u;
    return ZX_OK;
  }
  return status;
}

static zx_status_t zxio_pipe_write(zxio_t* io, const void* buffer, size_t capacity,
                                   size_t* out_actual) {
  zxio_pipe_t* pipe = reinterpret_cast<zxio_pipe_t*>(io);
  return pipe->socket.write(0, buffer, capacity, out_actual);
}

static constexpr zxio_ops_t zxio_pipe_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = zxio_pipe_close;
  ops.release = zxio_pipe_release;
  ops.clone = zxio_pipe_clone;
  ops.wait_begin = zxio_pipe_wait_begin;
  ops.wait_end = zxio_pipe_wait_end;
  ops.attr_get = zxio_pipe_attr_get;
  ops.read = zxio_pipe_read;
  ops.write = zxio_pipe_write;
  return ops;
}();

zx_status_t zxio_pipe_init(zxio_storage_t* storage, zx::socket socket) {
  zxio_pipe_t* pipe = reinterpret_cast<zxio_pipe_t*>(storage);
  zxio_init(&pipe->io, &zxio_pipe_ops);
  pipe->socket = std::move(socket);
  return ZX_OK;
}

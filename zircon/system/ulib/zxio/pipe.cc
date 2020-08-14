// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <sys/stat.h>
#include <zircon/syscalls.h>

#include "private.h"

static zx_status_t zxio_pipe_destroy(zxio_t* io) {
  auto pipe = reinterpret_cast<zxio_pipe_t*>(io);
  pipe->~zxio_pipe_t();
  return ZX_OK;
}

static zx_status_t zxio_pipe_close(zxio_t* io) {
  auto pipe = reinterpret_cast<zxio_pipe_t*>(io);
  // TODO(fxbug.dev/45407): We should mark the handle as "detached", instead of closing
  // the handle with risks of race behavior.
  pipe->socket.reset();
  return ZX_OK;
}

static zx_status_t zxio_pipe_release(zxio_t* io, zx_handle_t* out_handle) {
  auto pipe = reinterpret_cast<zxio_pipe_t*>(io);
  *out_handle = pipe->socket.release();
  return ZX_OK;
}

static zx_status_t zxio_pipe_clone(zxio_t* io, zx_handle_t* out_handle) {
  zx::socket out_socket;
  zx_status_t status =
      reinterpret_cast<zxio_pipe_t*>(io)->socket.duplicate(ZX_RIGHT_SAME_RIGHTS, &out_socket);
  if (status != ZX_OK) {
    return status;
  }
  *out_handle = out_socket.release();
  return ZX_OK;
}

static zx_status_t zxio_pipe_attr_get(zxio_t* io, zxio_node_attributes_t* out_attr) {
  *out_attr = {};
  ZXIO_NODE_ATTR_SET(*out_attr, protocols, ZXIO_NODE_PROTOCOL_PIPE);
  ZXIO_NODE_ATTR_SET(
      *out_attr, abilities,
      ZXIO_OPERATION_READ_BYTES | ZXIO_OPERATION_WRITE_BYTES | ZXIO_OPERATION_GET_ATTRIBUTES);
  return ZX_OK;
}

static void zxio_pipe_wait_begin(zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                                 zx_signals_t* out_zx_signals) {
  auto pipe = reinterpret_cast<zxio_pipe_t*>(io);
  *out_handle = pipe->socket.get();

  zx_signals_t zx_signals = ZX_SIGNAL_NONE;
  if (zxio_signals & ZXIO_SIGNAL_READABLE) {
    zx_signals |= ZX_SOCKET_READABLE;
  }
  if (zxio_signals & ZXIO_SIGNAL_WRITABLE) {
    zx_signals |= ZX_SOCKET_WRITABLE;
  }
  if (zxio_signals & ZXIO_SIGNAL_READ_DISABLED) {
    zx_signals |= ZX_SOCKET_PEER_WRITE_DISABLED;
  }
  if (zxio_signals & ZXIO_SIGNAL_WRITE_DISABLED) {
    zx_signals |= ZX_SOCKET_WRITE_DISABLED;
  }
  if (zxio_signals & ZXIO_SIGNAL_READ_THRESHOLD) {
    zx_signals |= ZX_SOCKET_READ_THRESHOLD;
  }
  if (zxio_signals & ZXIO_SIGNAL_WRITE_THRESHOLD) {
    zx_signals |= ZX_SOCKET_WRITE_THRESHOLD;
  }
  if (zxio_signals & ZXIO_SIGNAL_PEER_CLOSED) {
    zx_signals |= ZX_SOCKET_PEER_CLOSED;
  }
  *out_zx_signals = zx_signals;
}

static void zxio_pipe_wait_end(zxio_t* io, zx_signals_t zx_signals,
                               zxio_signals_t* out_zxio_signals) {
  zxio_signals_t zxio_signals = ZXIO_SIGNAL_NONE;
  if (zx_signals & ZX_SOCKET_READABLE) {
    zxio_signals |= ZXIO_SIGNAL_READABLE;
  }
  if (zx_signals & ZX_SOCKET_WRITABLE) {
    zxio_signals |= ZXIO_SIGNAL_WRITABLE;
  }
  if (zx_signals & ZX_SOCKET_PEER_WRITE_DISABLED) {
    zxio_signals |= ZXIO_SIGNAL_READ_DISABLED;
  }
  if (zx_signals & ZX_SOCKET_WRITE_DISABLED) {
    zxio_signals |= ZXIO_SIGNAL_WRITE_DISABLED;
  }
  if (zx_signals & ZX_SOCKET_READ_THRESHOLD) {
    zxio_signals |= ZXIO_SIGNAL_READ_THRESHOLD;
  }
  if (zx_signals & ZX_SOCKET_WRITE_THRESHOLD) {
    zxio_signals |= ZXIO_SIGNAL_WRITE_THRESHOLD;
  }
  if (zx_signals & ZX_SOCKET_PEER_CLOSED) {
    zxio_signals |= ZXIO_SIGNAL_PEER_CLOSED;
  }
  *out_zxio_signals = zxio_signals;
}

static zx_status_t zxio_pipe_read_status(zx_status_t status, size_t* out_actual) {
  // We've reached end-of-file, which is signaled by successfully reading zero
  // bytes.
  //
  // If we see |ZX_ERR_BAD_STATE|, that implies reading has been disabled for
  // this endpoint.
  if (status == ZX_ERR_PEER_CLOSED || status == ZX_ERR_BAD_STATE) {
    *out_actual = 0;
    status = ZX_OK;
  }
  return status;
}

zx_status_t zxio_datagram_pipe_readv(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                     zxio_flags_t flags, size_t* out_actual) {
  uint32_t zx_flags = 0;
  if (flags & ZXIO_PEEK) {
    zx_flags |= ZX_SOCKET_PEEK;
    flags &= ~ZXIO_PEEK;
  }
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto pipe = reinterpret_cast<zxio_pipe_t*>(io);

  size_t total = 0;
  for (size_t i = 0; i < vector_count; ++i) {
    total += vector[i].capacity;
  }
  std::unique_ptr<uint8_t[]> buf(new uint8_t[total]);

  size_t actual;
  zx_status_t status = pipe->socket.read(zx_flags, buf.get(), total, &actual);
  if (status != ZX_OK) {
    return zxio_pipe_read_status(status, out_actual);
  }

  uint8_t* data = buf.get();
  size_t remaining = actual;
  return zxio_do_vector(vector, vector_count, out_actual,
                        [&](void* buffer, size_t capacity, size_t* out_actual) {
                          size_t actual = std::min(capacity, remaining);
                          memcpy(buffer, data, actual);
                          data += actual;
                          remaining -= actual;
                          *out_actual = actual;
                          return ZX_OK;
                        });
}

zx_status_t zxio_datagram_pipe_writev(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                      zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto pipe = reinterpret_cast<zxio_pipe_t*>(io);

  size_t total = 0;
  for (size_t i = 0; i < vector_count; ++i) {
    total += vector[i].capacity;
  }
  std::unique_ptr<uint8_t[]> buf(new uint8_t[total]);

  uint8_t* data = buf.get();
  for (size_t i = 0; i < vector_count; ++i) {
    memcpy(data, vector[i].buffer, vector[i].capacity);
    data += vector[i].capacity;
  }

  return pipe->socket.write(0, buf.get(), total, out_actual);
}

zx_status_t zxio_stream_pipe_readv(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                   zxio_flags_t flags, size_t* out_actual) {
  if (flags & ZXIO_PEEK) {
    return zxio_datagram_pipe_readv(io, vector, vector_count, flags, out_actual);
  }
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto pipe = reinterpret_cast<zxio_pipe_t*>(io);

  return zxio_pipe_read_status(
      zxio_do_vector(vector, vector_count, out_actual,
                     [&](void* buffer, size_t capacity, size_t* out_actual) {
                       return pipe->socket.read(0, buffer, capacity, out_actual);
                     }),
      out_actual);
}

zx_status_t zxio_stream_pipe_writev(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                                    zxio_flags_t flags, size_t* out_actual) {
  if (flags) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  auto pipe = reinterpret_cast<zxio_pipe_t*>(io);

  return zxio_do_vector(vector, vector_count, out_actual,
                        [&](void* buffer, size_t capacity, size_t* out_actual) {
                          return pipe->socket.write(0, buffer, capacity, out_actual);
                        });
}

static constexpr zxio_ops_t zxio_pipe_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.destroy = zxio_pipe_destroy;
  ops.close = zxio_pipe_close;
  ops.release = zxio_pipe_release;
  ops.clone = zxio_pipe_clone;
  ops.wait_begin = zxio_pipe_wait_begin;
  ops.wait_end = zxio_pipe_wait_end;
  ops.attr_get = zxio_pipe_attr_get;
  return ops;
}();

static constexpr zxio_ops_t zxio_datagram_pipe_ops = []() {
  zxio_ops_t ops = zxio_pipe_ops;
  ops.readv = zxio_datagram_pipe_readv;
  ops.writev = zxio_datagram_pipe_writev;
  return ops;
}();

static constexpr zxio_ops_t zxio_stream_pipe_ops = []() {
  zxio_ops_t ops = zxio_pipe_ops;
  ops.readv = zxio_stream_pipe_readv;
  ops.writev = zxio_stream_pipe_writev;
  return ops;
}();

zx_status_t zxio_pipe_init(zxio_storage_t* storage, zx::socket socket, zx_info_socket_t info) {
  auto pipe = new (storage) zxio_pipe_t{
      .io = storage->io,
      .socket = std::move(socket),
  };
  zxio_init(&pipe->io,
            info.options & ZX_SOCKET_DATAGRAM ? &zxio_datagram_pipe_ops : &zxio_stream_pipe_ops);
  return ZX_OK;
}

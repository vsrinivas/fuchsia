// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <lib/zxio/ops.h>
#include <sys/socket.h>
#include <zircon/syscalls.h>

#include "private.h"

namespace fio = ::llcpp::fuchsia::io;

static zx_status_t zxio_socket_close(zxio_t* io) {
  auto zs = reinterpret_cast<zxio_socket_t*>(io);
  auto result = zs->control.Close();
  zs->~zxio_socket_t();
  return result.ok() ? result->s : result.status();
}

static zx_status_t zxio_socket_release(zxio_t* io, zx_handle_t* out_handle) {
  auto zs = reinterpret_cast<zxio_socket_t*>(io);
  *out_handle = zs->control.mutable_channel()->release();
  zs->~zxio_socket_t();
  return ZX_OK;
}

static zx_status_t zxio_socket_clone(zxio_t* io, zx_handle_t* out_handle) {
  auto zs = reinterpret_cast<zxio_socket_t*>(io);
  zx::channel local, remote;
  zx_status_t status = zx::channel::create(0, &local, &remote);
  if (status != ZX_OK) {
    return status;
  }
  status = zs->control.Clone(fio::CLONE_FLAG_SAME_RIGHTS, std::move(remote)).status();
  if (status != ZX_OK) {
    return status;
  }
  *out_handle = local.release();
  return ZX_OK;
}

static constexpr zxio_ops_t zxio_socket_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = zxio_socket_close;
  ops.release = zxio_socket_release;
  ops.clone = zxio_socket_clone;
  return ops;
}();

static constexpr zxio_ops_t zxio_datagram_socket_ops = []() {
  zxio_ops_t ops = zxio_socket_ops;
  ops.read_vector = zxio_datagram_pipe_read_vector;
  ops.write_vector = zxio_datagram_pipe_write_vector;
  return ops;
}();

static constexpr zxio_ops_t zxio_stream_socket_ops = []() {
  zxio_ops_t ops = zxio_socket_ops;
  ops.read_vector = zxio_stream_pipe_read_vector;
  ops.write_vector = zxio_stream_pipe_write_vector;
  return ops;
}();

zx_status_t zxio_socket_init(zxio_storage_t* storage,
                             ::llcpp::fuchsia::posix::socket::Control::SyncClient control,
                             zx::socket socket, zx_info_socket_t info) {
  auto zs = new (storage) zxio_socket_t{
      .pipe =
          {
              .io = storage->io,
              .socket = std::move(socket),
          },
      .control = std::move(control),
  };
  zxio_init(&zs->pipe.io, info.options & ZX_SOCKET_DATAGRAM ? &zxio_datagram_socket_ops
                                                            : &zxio_stream_socket_ops);
  return ZX_OK;
}

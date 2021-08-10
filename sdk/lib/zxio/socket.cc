// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/llcpp/connect_service.h>
#include <lib/zx/socket.h>
#include <lib/zxio/cpp/inception.h>
#include <lib/zxio/cpp/vector.h>
#include <lib/zxio/null.h>

#include "sdk/lib/zxio/private.h"

namespace fio = fuchsia_io;
namespace fsocket = fuchsia_posix_socket;
namespace frawsocket = fuchsia_posix_socket_raw;

namespace {

template <typename Client,
          typename = std::enable_if_t<
              std::is_same_v<Client, fidl::WireSyncClient<fsocket::DatagramSocket>> ||
              std::is_same_v<Client, fidl::WireSyncClient<fsocket::StreamSocket>> ||
              std::is_same_v<Client, fidl::WireSyncClient<frawsocket::Socket>>>>
class BaseSocket {
  static_assert(std::is_same_v<Client, fidl::WireSyncClient<fsocket::DatagramSocket>> ||
                std::is_same_v<Client, fidl::WireSyncClient<fsocket::StreamSocket>> ||
                std::is_same_v<Client, fidl::WireSyncClient<frawsocket::Socket>>);

 public:
  explicit BaseSocket(Client& client) : client_(client) {}

  zx_status_t CloseSocket() {
    fidl::WireResult result = client_.Close();
    zx_status_t status;
    if ((status = result.status()) != ZX_OK) {
      return status;
    }
    if ((status = result->s) != ZX_OK) {
      return status;
    }
    if ((status = client_.channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(),
                                             nullptr)) != ZX_OK) {
      return status;
    }
    return ZX_OK;
  }

  zx_status_t CloneSocket(zx_handle_t* out_handle) {
    zx::status endpoints = fidl::CreateEndpoints<fio::Node>();
    if (endpoints.is_error()) {
      return endpoints.status_value();
    }
    zx_status_t status =
        client_.Clone(fio::wire::kCloneFlagSameRights, std::move(endpoints->server)).status();
    if (status != ZX_OK) {
      return status;
    }
    *out_handle = endpoints->client.channel().release();
    return ZX_OK;
  }

 private:
  Client& client_;
};

}  // namespace

static zxio_datagram_socket_t& zxio_datagram_socket(zxio_t* io) {
  return *reinterpret_cast<zxio_datagram_socket_t*>(io);
}

static constexpr zxio_ops_t zxio_datagram_socket_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = [](zxio_t* io) {
    zxio_datagram_socket_t& zs = zxio_datagram_socket(io);
    zx_status_t status = ZX_OK;
    if (zs.client.channel().is_valid()) {
      status = BaseSocket(zs.client).CloseSocket();
    }
    zs.~zxio_datagram_socket_t();
    return status;
  };
  ops.release = [](zxio_t* io, zx_handle_t* out_handle) {
    if (out_handle == nullptr) {
      return ZX_ERR_INVALID_ARGS;
    }
    *out_handle = zxio_datagram_socket(io).client.mutable_channel()->release();
    return ZX_OK;
  };
  ops.borrow = [](zxio_t* io, zx_handle_t* out_handle) {
    *out_handle = zxio_datagram_socket(io).client.channel().get();
    return ZX_OK;
  };
  ops.clone = [](zxio_t* io, zx_handle_t* out_handle) {
    if (out_handle == nullptr) {
      return ZX_ERR_INVALID_ARGS;
    }
    zxio_datagram_socket_t& zs = zxio_datagram_socket(io);
    zx_status_t status = BaseSocket(zs.client).CloneSocket(out_handle);
    return status;
  };
  return ops;
}();

zx_status_t zxio_datagram_socket_init(
    zxio_storage_t* storage, zx::eventpair event,
    fidl::ClientEnd<fuchsia_posix_socket::DatagramSocket> client) {
  auto zs = new (storage) zxio_datagram_socket_t{
      .io = storage->io,
      .event = std::move(event),
      .client = fidl::BindSyncClient(std::move(client)),
  };
  zxio_init(&zs->io, &zxio_datagram_socket_ops);
  return ZX_OK;
}

static zxio_stream_socket_t& zxio_stream_socket(zxio_t* io) {
  return *reinterpret_cast<zxio_stream_socket_t*>(io);
}

static constexpr zxio_ops_t zxio_stream_socket_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = [](zxio_t* io) {
    zxio_stream_socket_t& zs = zxio_stream_socket(io);
    zx_status_t status = ZX_OK;
    if (zs.client.channel().is_valid()) {
      status = BaseSocket(zs.client).CloseSocket();
    }
    zs.~zxio_stream_socket_t();
    return status;
  };
  ops.release = [](zxio_t* io, zx_handle_t* out_handle) {
    if (out_handle == nullptr) {
      return ZX_ERR_INVALID_ARGS;
    }
    *out_handle = zxio_stream_socket(io).client.mutable_channel()->release();
    return ZX_OK;
  };
  ops.borrow = [](zxio_t* io, zx_handle_t* out_handle) {
    *out_handle = zxio_stream_socket(io).client.channel().get();
    return ZX_OK;
  };
  ops.clone = [](zxio_t* io, zx_handle_t* out_handle) {
    if (out_handle == nullptr) {
      return ZX_ERR_INVALID_ARGS;
    }
    return BaseSocket(zxio_stream_socket(io).client).CloneSocket(out_handle);
  };
  ops.wait_begin = [](zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                      zx_signals_t* out_zx_signals) {
    zxio_wait_begin(&zxio_stream_socket(io).pipe.io, zxio_signals, out_handle, out_zx_signals);
  };
  ops.wait_end = [](zxio_t* io, zx_signals_t zx_signals, zxio_signals_t* out_zxio_signals) {
    zxio_wait_end(&zxio_stream_socket(io).pipe.io, zx_signals, out_zxio_signals);
  };
  ops.readv = [](zxio_t* io, const zx_iovec_t* vector, size_t vector_count, zxio_flags_t flags,
                 size_t* out_actual) {
    zx::socket& socket = zxio_stream_socket(io).pipe.socket;

    if (flags & ZXIO_PEEK) {
      uint32_t zx_flags = ZX_SOCKET_PEEK;
      flags &= ~ZXIO_PEEK;

      if (flags) {
        return ZX_ERR_NOT_SUPPORTED;
      }

      size_t total = 0;
      for (size_t i = 0; i < vector_count; ++i) {
        total += vector[i].capacity;
      }
      std::unique_ptr<uint8_t[]> buf(new uint8_t[total]);

      size_t actual;
      zx_status_t status = socket.read(zx_flags, buf.get(), total, &actual);
      if (status != ZX_OK) {
        return status;
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

    if (flags) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    return zxio_do_vector(vector, vector_count, out_actual,
                          [&](void* buffer, size_t capacity, size_t* out_actual) {
                            return socket.read(0, buffer, capacity, out_actual);
                          });
  };
  ops.writev = [](zxio_t* io, const zx_iovec_t* vector, size_t vector_count, zxio_flags_t flags,
                  size_t* out_actual) {
    return zxio_writev(&zxio_stream_socket(io).pipe.io, vector, vector_count, flags, out_actual);
  };
  ops.get_read_buffer_available = [](zxio_t* io, size_t* out_available) {
    return zxio_get_read_buffer_available(&zxio_stream_socket(io).pipe.io, out_available);
  };
  ops.shutdown = [](zxio_t* io, zxio_shutdown_options_t options) {
    return zxio_shutdown(&zxio_stream_socket(io).pipe.io, options);
  };
  return ops;
}();

zx_status_t zxio_stream_socket_init(zxio_storage_t* storage, zx::socket socket,
                                    fidl::ClientEnd<fuchsia_posix_socket::StreamSocket> client,
                                    zx_info_socket_t& info) {
  auto zs = new (storage) zxio_stream_socket_t{
      .io = {},
      .pipe = {},
      .client = fidl::BindSyncClient(std::move(client)),
  };
  zxio_init(&zs->io, &zxio_stream_socket_ops);
  return zxio_pipe_init(reinterpret_cast<zxio_storage_t*>(&zs->pipe), std::move(socket), info);
}

static zxio_raw_socket_t& zxio_raw_socket(zxio_t* io) {
  return *reinterpret_cast<zxio_raw_socket_t*>(io);
}

static constexpr zxio_ops_t zxio_raw_socket_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = [](zxio_t* io) {
    zxio_raw_socket_t& zs = zxio_raw_socket(io);
    zx_status_t status = ZX_OK;
    if (zs.client.channel().is_valid()) {
      status = BaseSocket(zs.client).CloseSocket();
    }
    zs.~zxio_raw_socket_t();
    return status;
  };
  ops.release = [](zxio_t* io, zx_handle_t* out_handle) {
    if (out_handle == nullptr) {
      return ZX_ERR_INVALID_ARGS;
    }
    *out_handle = zxio_raw_socket(io).client.mutable_channel()->release();
    return ZX_OK;
  };
  ops.borrow = [](zxio_t* io, zx_handle_t* out_handle) {
    *out_handle = zxio_raw_socket(io).client.channel().get();
    return ZX_OK;
  };
  ops.clone = [](zxio_t* io, zx_handle_t* out_handle) {
    if (out_handle == nullptr) {
      return ZX_ERR_INVALID_ARGS;
    }
    zxio_raw_socket_t& zs = zxio_raw_socket(io);
    zx_status_t status = BaseSocket(zs.client).CloneSocket(out_handle);
    return status;
  };
  return ops;
}();

zx_status_t zxio_raw_socket_init(zxio_storage_t* storage, zx::eventpair event,
                                 fidl::ClientEnd<fuchsia_posix_socket_raw::Socket> client) {
  auto zs = new (storage) zxio_raw_socket_t{
      .io = storage->io,
      .event = std::move(event),
      .client = fidl::BindSyncClient(std::move(client)),
  };
  zxio_init(&zs->io, &zxio_raw_socket_ops);
  return ZX_OK;
}

zx_status_t zxio_is_socket(zxio_t* io, bool* out_is_socket) {
  if (io == nullptr || out_is_socket == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  const zxio_ops_t* ops = zxio_get_ops(io);
  return ops == &zxio_datagram_socket_ops || ops == &zxio_stream_socket_ops ||
         ops == &zxio_raw_socket_ops;
}

// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fidl/cpp/wire/connect_service.h>
#include <lib/zx/socket.h>
#include <lib/zxio/cpp/inception.h>
#include <lib/zxio/cpp/socket_address.h>
#include <lib/zxio/cpp/vector.h>
#include <lib/zxio/null.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>

#include <safemath/safe_conversions.h>

#include "sdk/lib/zxio/private.h"

namespace fio = fuchsia_io;
namespace fsocket = fuchsia_posix_socket;
namespace frawsocket = fuchsia_posix_socket_raw;
namespace fpacketsocket = fuchsia_posix_socket_packet;
namespace fnet = fuchsia_net;

namespace {

uint16_t fidl_protoassoc_to_protocol(const fpacketsocket::wire::ProtocolAssociation& protocol) {
  // protocol has an invalid tag when it's not provided by the server (when the socket is not
  // associated).
  //
  // TODO(https://fxbug.dev/58503): Use better representation of nullable union when available.
  if (protocol.has_invalid_tag()) {
    return 0;
  }

  switch (protocol.Which()) {
    case fpacketsocket::wire::ProtocolAssociation::Tag::kAll:
      return ETH_P_ALL;
    case fpacketsocket::wire::ProtocolAssociation::Tag::kSpecified:
      return protocol.specified();
  }
}

template <typename Client,
          typename = std::enable_if_t<
              std::is_same_v<Client, fidl::WireSyncClient<fsocket::SynchronousDatagramSocket>> ||
              std::is_same_v<Client, fidl::WireSyncClient<fsocket::DatagramSocket>> ||
              std::is_same_v<Client, fidl::WireSyncClient<fsocket::StreamSocket>> ||
              std::is_same_v<Client, fidl::WireSyncClient<frawsocket::Socket>> ||
              std::is_same_v<Client, fidl::WireSyncClient<fpacketsocket::Socket>>>>
class BaseSocket {
  static_assert(std::is_same_v<Client, fidl::WireSyncClient<fsocket::SynchronousDatagramSocket>> ||
                std::is_same_v<Client, fidl::WireSyncClient<fsocket::DatagramSocket>> ||
                std::is_same_v<Client, fidl::WireSyncClient<fsocket::StreamSocket>> ||
                std::is_same_v<Client, fidl::WireSyncClient<frawsocket::Socket>> ||
                std::is_same_v<Client, fidl::WireSyncClient<fpacketsocket::Socket>>);

 public:
  explicit BaseSocket(Client& client) : client_(client) {}

  Client& client() { return client_; }

  zx_status_t CloseSocket() {
    const fidl::WireResult result = client_->Close();
    if (!result.ok()) {
      return result.status();
    }
    const auto& response = result.value();
    if (response.is_error()) {
      return response.error_value();
    }
    return client_.client_end().channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(),
                                                   nullptr);
  }

  zx_status_t CloneSocket(zx_handle_t* out_handle) {
    zx::status endpoints = fidl::CreateEndpoints<fio::Node>();
    if (endpoints.is_error()) {
      return endpoints.status_value();
    }
    zx_status_t status =
        client_->Clone(fio::wire::OpenFlags::kCloneSameRights, std::move(endpoints->server))
            .status();
    if (status != ZX_OK) {
      return status;
    }
    *out_handle = endpoints->client.channel().release();
    return ZX_OK;
  }

 private:
  Client& client_;
};

template <typename T,
          typename = std::enable_if_t<
              std::is_same_v<T, fidl::WireSyncClient<fsocket::SynchronousDatagramSocket>> ||
              std::is_same_v<T, fidl::WireSyncClient<fsocket::StreamSocket>> ||
              std::is_same_v<T, fidl::WireSyncClient<frawsocket::Socket>> ||
              std::is_same_v<T, fidl::WireSyncClient<fsocket::DatagramSocket>>>>
struct BaseNetworkSocket : public BaseSocket<T> {
  static_assert(std::is_same_v<T, fidl::WireSyncClient<fsocket::SynchronousDatagramSocket>> ||
                std::is_same_v<T, fidl::WireSyncClient<fsocket::StreamSocket>> ||
                std::is_same_v<T, fidl::WireSyncClient<frawsocket::Socket>> ||
                std::is_same_v<T, fidl::WireSyncClient<fsocket::DatagramSocket>>);

 public:
  using BaseSocket = BaseSocket<T>;
  using BaseSocket::client;

  explicit BaseNetworkSocket(T& client) : BaseSocket(client) {}

  zx_status_t bind(const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    SocketAddress fidl_addr;
    zx_status_t status = fidl_addr.LoadSockAddr(addr, addrlen);
    if (status != ZX_OK) {
      return status;
    }

    auto response = fidl_addr.WithFIDL(
        [this](fnet::wire::SocketAddress address) { return client()->Bind(address); });
    status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    auto const& result = response.value();
    if (result.is_error()) {
      *out_code = static_cast<int16_t>(result.error_value());
      return ZX_OK;
    }
    *out_code = 0;
    return ZX_OK;
  }

  zx_status_t connect(const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    // If address is AF_UNSPEC we should call disconnect.
    if (addr->sa_family == AF_UNSPEC) {
      auto response = client()->Disconnect();
      zx_status_t status = response.status();
      if (status != ZX_OK) {
        return status;
      }
      const auto& result = response.value();
      if (result.is_error()) {
        *out_code = static_cast<int16_t>(result.error_value());
      } else {
        *out_code = 0;
      }
      return ZX_OK;
    }

    SocketAddress fidl_addr;
    zx_status_t status = fidl_addr.LoadSockAddr(addr, addrlen);
    if (status != ZX_OK) {
      return status;
    }

    auto response = fidl_addr.WithFIDL(
        [this](fnet::wire::SocketAddress address) { return client()->Connect(address); });
    status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    auto const& result = response.value();
    if (result.is_error()) {
      *out_code = static_cast<int16_t>(result.error_value());
    } else {
      *out_code = 0;
    }
    return ZX_OK;
  }

  template <typename R>
  zx_status_t getname(R&& response, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    auto const& result = response.value();
    if (result.is_error()) {
      *out_code = static_cast<int16_t>(result.error_value());
      return ZX_OK;
    }
    if (addrlen == nullptr || (*addrlen != 0 && addr == nullptr)) {
      *out_code = EFAULT;
      return ZX_OK;
    }
    *out_code = 0;
    auto const& out = result.value()->addr;
    *addrlen = zxio_fidl_to_sockaddr(out, addr, *addrlen);
    return ZX_OK;
  }

  zx_status_t getsockname(struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return getname(client()->GetSockName(), addr, addrlen, out_code);
  }

  zx_status_t getpeername(struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return getname(client()->GetPeerName(), addr, addrlen, out_code);
  }
};

}  // namespace

static zxio_synchronous_datagram_socket_t& zxio_synchronous_datagram_socket(zxio_t* io) {
  return *reinterpret_cast<zxio_synchronous_datagram_socket_t*>(io);
}

static constexpr zxio_ops_t zxio_synchronous_datagram_socket_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = [](zxio_t* io) {
    zxio_synchronous_datagram_socket_t& zs = zxio_synchronous_datagram_socket(io);
    zx_status_t status = ZX_OK;
    if (zs.client.is_valid()) {
      status = BaseSocket(zs.client).CloseSocket();
    }
    zs.~zxio_synchronous_datagram_socket_t();
    return status;
  };
  ops.release = [](zxio_t* io, zx_handle_t* out_handle) {
    if (out_handle == nullptr) {
      return ZX_ERR_INVALID_ARGS;
    }
    *out_handle =
        zxio_synchronous_datagram_socket(io).client.TakeClientEnd().TakeChannel().release();
    return ZX_OK;
  };
  ops.borrow = [](zxio_t* io, zx_handle_t* out_handle) {
    *out_handle =
        zxio_synchronous_datagram_socket(io).client.client_end().borrow().channel()->get();
    return ZX_OK;
  };
  ops.clone = [](zxio_t* io, zx_handle_t* out_handle) {
    zxio_synchronous_datagram_socket_t& zs = zxio_synchronous_datagram_socket(io);
    zx_status_t status = BaseSocket(zs.client).CloneSocket(out_handle);
    return status;
  };
  ops.bind = [](zxio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    return BaseNetworkSocket(zxio_synchronous_datagram_socket(io).client)
        .bind(addr, addrlen, out_code);
  };
  ops.connect = [](zxio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    return BaseNetworkSocket(zxio_synchronous_datagram_socket(io).client)
        .connect(addr, addrlen, out_code);
  };
  ops.getsockname = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return BaseNetworkSocket(zxio_synchronous_datagram_socket(io).client)
        .getsockname(addr, addrlen, out_code);
  };
  ops.getpeername = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return BaseNetworkSocket(zxio_synchronous_datagram_socket(io).client)
        .getpeername(addr, addrlen, out_code);
  };
  return ops;
}();

zx_status_t zxio_synchronous_datagram_socket_init(
    zxio_storage_t* storage, zx::eventpair event,
    fidl::ClientEnd<fsocket::SynchronousDatagramSocket> client) {
  auto zs = new (storage) zxio_synchronous_datagram_socket_t{
      .io = storage->io,
      .event = std::move(event),
      .client = fidl::BindSyncClient(std::move(client)),
  };
  zxio_init(&zs->io, &zxio_synchronous_datagram_socket_ops);
  return ZX_OK;
}

static zxio_datagram_socket_t& zxio_datagram_socket(zxio_t* io) {
  return *reinterpret_cast<zxio_datagram_socket_t*>(io);
}

static constexpr zxio_ops_t zxio_datagram_socket_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = [](zxio_t* io) {
    zxio_datagram_socket_t& zs = zxio_datagram_socket(io);
    zx_status_t status = ZX_OK;
    if (zs.client.is_valid()) {
      status = BaseSocket(zs.client).CloseSocket();
    }
    zs.~zxio_datagram_socket();
    return status;
  };
  ops.release = [](zxio_t* io, zx_handle_t* out_handle) {
    if (out_handle == nullptr) {
      return ZX_ERR_INVALID_ARGS;
    }
    *out_handle = zxio_datagram_socket(io).client.TakeClientEnd().TakeChannel().release();
    return ZX_OK;
  };
  ops.borrow = [](zxio_t* io, zx_handle_t* out_handle) {
    *out_handle = zxio_datagram_socket(io).client.client_end().borrow().channel()->get();
    return ZX_OK;
  };
  ops.clone = [](zxio_t* io, zx_handle_t* out_handle) {
    return BaseSocket(zxio_datagram_socket(io).client).CloneSocket(out_handle);
  };
  ops.wait_begin = [](zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                      zx_signals_t* out_zx_signals) {
    zxio_wait_begin(&zxio_datagram_socket(io).pipe.io, zxio_signals, out_handle, out_zx_signals);
  };
  ops.wait_end = [](zxio_t* io, zx_signals_t zx_signals, zxio_signals_t* out_zxio_signals) {
    zxio_wait_end(&zxio_datagram_socket(io).pipe.io, zx_signals, out_zxio_signals);
  };
  ops.readv = [](zxio_t* io, const zx_iovec_t* vector, size_t vector_count, zxio_flags_t flags,
                 size_t* out_actual) {
    return zxio_readv(&zxio_datagram_socket(io).pipe.io, vector, vector_count, flags, out_actual);
  };
  ops.writev = [](zxio_t* io, const zx_iovec_t* vector, size_t vector_count, zxio_flags_t flags,
                  size_t* out_actual) {
    return zxio_writev(&zxio_datagram_socket(io).pipe.io, vector, vector_count, flags, out_actual);
  };
  ops.shutdown = [](zxio_t* io, zxio_shutdown_options_t options) {
    return zxio_shutdown(&zxio_datagram_socket(io).pipe.io, options);
  };
  ops.bind = [](zxio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    return BaseNetworkSocket(zxio_datagram_socket(io).client).bind(addr, addrlen, out_code);
  };
  ops.connect = [](zxio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    return BaseNetworkSocket(zxio_datagram_socket(io).client).connect(addr, addrlen, out_code);
  };
  ops.getsockname = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return BaseNetworkSocket(zxio_datagram_socket(io).client).getsockname(addr, addrlen, out_code);
  };
  ops.getpeername = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return BaseNetworkSocket(zxio_datagram_socket(io).client).getpeername(addr, addrlen, out_code);
  };
  return ops;
}();

zx_status_t zxio_datagram_socket_init(zxio_storage_t* storage, zx::socket socket,
                                      const zx_info_socket_t& info,
                                      const zxio_datagram_prelude_size_t& prelude_size,
                                      fidl::ClientEnd<fsocket::DatagramSocket> client) {
  auto zs = new (storage) zxio_datagram_socket_t{
      .io = {},
      .pipe = {},
      .prelude_size = prelude_size,
      .client = fidl::BindSyncClient(std::move(client)),
  };
  zxio_init(&zs->io, &zxio_datagram_socket_ops);
  return zxio_pipe_init(reinterpret_cast<zxio_storage_t*>(&zs->pipe), std::move(socket), info);
}

static zxio_stream_socket_t& zxio_stream_socket(zxio_t* io) {
  return *reinterpret_cast<zxio_stream_socket_t*>(io);
}

static constexpr zxio_ops_t zxio_stream_socket_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = [](zxio_t* io) {
    zxio_stream_socket_t& zs = zxio_stream_socket(io);
    zx_status_t status = ZX_OK;
    if (zs.client.is_valid()) {
      status = BaseSocket(zs.client).CloseSocket();
    }
    zs.~zxio_stream_socket_t();
    return status;
  };
  ops.release = [](zxio_t* io, zx_handle_t* out_handle) {
    if (out_handle == nullptr) {
      return ZX_ERR_INVALID_ARGS;
    }
    *out_handle = zxio_stream_socket(io).client.TakeClientEnd().TakeChannel().release();
    return ZX_OK;
  };
  ops.borrow = [](zxio_t* io, zx_handle_t* out_handle) {
    *out_handle = zxio_stream_socket(io).client.client_end().borrow().channel()->get();
    return ZX_OK;
  };
  ops.clone = [](zxio_t* io, zx_handle_t* out_handle) {
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
  ops.bind = [](zxio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    return BaseNetworkSocket(zxio_stream_socket(io).client).bind(addr, addrlen, out_code);
  };
  ops.connect = [](zxio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    zx_status_t status =
        BaseNetworkSocket(zxio_stream_socket(io).client).connect(addr, addrlen, out_code);
    if (status == ZX_OK) {
      std::lock_guard lock(zxio_stream_socket(io).state_lock);
      switch (*out_code) {
        case 0:
          zxio_stream_socket(io).state = zxio_stream_socket_state_t::CONNECTED;
          break;
        case EINPROGRESS:
          zxio_stream_socket(io).state = zxio_stream_socket_state_t::CONNECTING;
          break;
      }
    }
    return status;
  };
  ops.listen = [](zxio_t* io, int backlog, int16_t* out_code) {
    auto response =
        zxio_stream_socket(io).client->Listen(safemath::saturated_cast<int16_t>(backlog));
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    auto const& result = response.value();
    if (result.is_error()) {
      *out_code = static_cast<int16_t>(result.error_value());
      return ZX_OK;
    }
    {
      std::lock_guard lock(zxio_stream_socket(io).state_lock);
      zxio_stream_socket(io).state = zxio_stream_socket_state_t::LISTENING;
    }
    *out_code = 0;
    return ZX_OK;
  };
  ops.accept = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen,
                  zxio_storage_t* out_storage, int16_t* out_code) {
    bool want_addr = addr != nullptr && addrlen != nullptr;
    auto response = zxio_stream_socket(io).client->Accept(want_addr);
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    const auto& result = response.value();
    if (result.is_error()) {
      *out_code = static_cast<int16_t>(result.error_value());
      return ZX_OK;
    }
    *out_code = 0;
    auto const& out = result.value()->addr;
    // Result address has invalid tag when it's not provided by the server (when want_addr
    // is false).
    // TODO(https://fxbug.dev/58503): Use better representation of nullable union when available.
    if (want_addr && !out.has_invalid_tag()) {
      *addrlen = static_cast<socklen_t>(zxio_fidl_to_sockaddr(out, addr, *addrlen));
    }

    fidl::ClientEnd<fsocket::StreamSocket>& control = result.value()->s;
    fidl::WireResult describe_result = fidl::WireCall(control)->Describe2();
    if (!describe_result.ok()) {
      return describe_result.status();
    }
    fidl::WireResponse describe_response = describe_result.value();
    if (!describe_response.has_socket()) {
      return ZX_ERR_NOT_SUPPORTED;
    }

    zx::socket& socket = describe_response.socket();
    zx_info_socket_t info;
    if (zx_status_t status = socket.get_info(ZX_INFO_SOCKET, &info, sizeof(info), nullptr, nullptr);
        status != ZX_OK) {
      return status;
    }
    if (zx_status_t status = zxio_stream_socket_init(out_storage, std::move(socket), info,
                                                     /*is_connected=*/true, std::move(control));
        status != ZX_OK) {
      return status;
    }
    return ZX_OK;
  };
  ops.getsockname = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return BaseNetworkSocket(zxio_stream_socket(io).client).getsockname(addr, addrlen, out_code);
  };
  ops.getpeername = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return BaseNetworkSocket(zxio_stream_socket(io).client).getpeername(addr, addrlen, out_code);
  };
  return ops;
}();

zx_status_t zxio_stream_socket_init(zxio_storage_t* storage, zx::socket socket,
                                    const zx_info_socket_t& info, const bool is_connected,
                                    fidl::ClientEnd<fsocket::StreamSocket> client) {
  zxio_stream_socket_state_t state = is_connected ? zxio_stream_socket_state_t::CONNECTED
                                                  : zxio_stream_socket_state_t::UNCONNECTED;
  auto zs = new (storage) zxio_stream_socket_t{
      .io = {},
      .pipe = {},
      .state_lock = {},
      .state = state,
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
    if (zs.client.is_valid()) {
      status = BaseSocket(zs.client).CloseSocket();
    }
    zs.~zxio_raw_socket_t();
    return status;
  };
  ops.release = [](zxio_t* io, zx_handle_t* out_handle) {
    if (out_handle == nullptr) {
      return ZX_ERR_INVALID_ARGS;
    }
    *out_handle = zxio_raw_socket(io).client.TakeClientEnd().TakeChannel().release();
    return ZX_OK;
  };
  ops.borrow = [](zxio_t* io, zx_handle_t* out_handle) {
    *out_handle = zxio_raw_socket(io).client.client_end().borrow().channel()->get();
    return ZX_OK;
  };
  ops.clone = [](zxio_t* io, zx_handle_t* out_handle) {
    zxio_raw_socket_t& zs = zxio_raw_socket(io);
    zx_status_t status = BaseSocket(zs.client).CloneSocket(out_handle);
    return status;
  };
  ops.bind = [](zxio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    return BaseNetworkSocket(zxio_raw_socket(io).client).bind(addr, addrlen, out_code);
  };
  ops.connect = [](zxio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    return BaseNetworkSocket(zxio_raw_socket(io).client).connect(addr, addrlen, out_code);
  };
  ops.getsockname = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return BaseNetworkSocket(zxio_raw_socket(io).client).getsockname(addr, addrlen, out_code);
  };
  ops.getpeername = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return BaseNetworkSocket(zxio_raw_socket(io).client).getpeername(addr, addrlen, out_code);
  };
  return ops;
}();

zx_status_t zxio_raw_socket_init(zxio_storage_t* storage, zx::eventpair event,
                                 fidl::ClientEnd<frawsocket::Socket> client) {
  auto zs = new (storage) zxio_raw_socket_t{
      .io = storage->io,
      .event = std::move(event),
      .client = fidl::BindSyncClient(std::move(client)),
  };
  zxio_init(&zs->io, &zxio_raw_socket_ops);
  return ZX_OK;
}

static zxio_packet_socket_t& zxio_packet_socket(zxio_t* io) {
  return *reinterpret_cast<zxio_packet_socket_t*>(io);
}

static constexpr zxio_ops_t zxio_packet_socket_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = [](zxio_t* io) {
    zxio_packet_socket_t& zs = zxio_packet_socket(io);
    zx_status_t status = ZX_OK;
    if (zs.client.is_valid()) {
      status = BaseSocket(zs.client).CloseSocket();
    }
    zs.~zxio_packet_socket_t();
    return status;
  };
  ops.release = [](zxio_t* io, zx_handle_t* out_handle) {
    if (out_handle == nullptr) {
      return ZX_ERR_INVALID_ARGS;
    }
    *out_handle = zxio_packet_socket(io).client.TakeClientEnd().TakeChannel().release();
    return ZX_OK;
  };
  ops.borrow = [](zxio_t* io, zx_handle_t* out_handle) {
    *out_handle = zxio_packet_socket(io).client.client_end().borrow().channel()->get();
    return ZX_OK;
  };
  ops.clone = [](zxio_t* io, zx_handle_t* out_handle) {
    zxio_packet_socket_t& zs = zxio_packet_socket(io);
    zx_status_t status = BaseSocket(zs.client).CloneSocket(out_handle);
    return status;
  };
  ops.bind = [](zxio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    if (addr == nullptr || addrlen < sizeof(sockaddr_ll)) {
      return ZX_ERR_INVALID_ARGS;
    }

    const sockaddr_ll& sll = *reinterpret_cast<const sockaddr_ll*>(addr);

    fpacketsocket::wire::ProtocolAssociation proto_assoc;
    uint16_t protocol = ntohs(sll.sll_protocol);
    switch (protocol) {
      case 0:
        // protocol association is optional.
        break;
      case ETH_P_ALL:
        proto_assoc =
            fpacketsocket::wire::ProtocolAssociation::WithAll(fpacketsocket::wire::Empty());
        break;
      default:
        proto_assoc = fpacketsocket::wire::ProtocolAssociation::WithSpecified(protocol);
        break;
    }

    fpacketsocket::wire::BoundInterfaceId interface_id;
    uint64_t ifindex = sll.sll_ifindex;
    if (ifindex == 0) {
      interface_id = fpacketsocket::wire::BoundInterfaceId::WithAll(fpacketsocket::wire::Empty());
    } else {
      interface_id = fpacketsocket::wire::BoundInterfaceId::WithSpecified(
          fidl::ObjectView<uint64_t>::FromExternal(&ifindex));
    }

    const fidl::WireResult response =
        zxio_packet_socket(io).client->Bind(proto_assoc, interface_id);
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    const auto& result = response.value();
    if (result.is_error()) {
      *out_code = static_cast<int16_t>(result.error_value());
      return ZX_OK;
    }
    *out_code = 0;
    return ZX_OK;
  };
  ops.connect = [](zxio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    return ZX_ERR_WRONG_TYPE;
  };
  ops.getsockname = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    if (addrlen == nullptr || (*addrlen != 0 && addr == nullptr)) {
      *out_code = EFAULT;
      return ZX_OK;
    }

    const fidl::WireResult response = zxio_packet_socket(io).client->GetInfo();
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    const auto& result = response.value();
    if (result.is_error()) {
      *out_code = static_cast<int16_t>(result.error_value());
      return ZX_OK;
    }
    *out_code = 0;

    const fpacketsocket::wire::SocketGetInfoResponse& info = *result.value();
    sockaddr_ll sll = {
        .sll_family = AF_PACKET,
        .sll_protocol = htons(fidl_protoassoc_to_protocol(info.protocol)),
    };

    switch (info.bound_interface.Which()) {
      case fpacketsocket::wire::BoundInterface::Tag::kAll:
        sll.sll_ifindex = 0;
        sll.sll_halen = 0;
        sll.sll_hatype = 0;
        break;
      case fpacketsocket::wire::BoundInterface::Tag::kSpecified: {
        const fpacketsocket::wire::InterfaceProperties& props = info.bound_interface.specified();
        sll.sll_ifindex = static_cast<int>(props.id);
        sll.sll_hatype = zxio_fidl_hwtype_to_arphrd(props.type);
        zxio_populate_from_fidl_hwaddr(props.addr, sll);
      } break;
    }

    socklen_t used_bytes = offsetof(sockaddr_ll, sll_addr) + sll.sll_halen;
    memcpy(addr, &sll, std::min(used_bytes, *addrlen));
    *addrlen = used_bytes;
    return ZX_OK;
  };
  ops.getpeername = [](zxio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return ZX_ERR_WRONG_TYPE;
  };
  return ops;
}();

zx_status_t zxio_packet_socket_init(zxio_storage_t* storage, zx::eventpair event,
                                    fidl::ClientEnd<fpacketsocket::Socket> client) {
  auto zs = new (storage) zxio_packet_socket_t{
      .io = storage->io,
      .event = std::move(event),
      .client = fidl::BindSyncClient(std::move(client)),
  };
  zxio_init(&zs->io, &zxio_packet_socket_ops);
  return ZX_OK;
}

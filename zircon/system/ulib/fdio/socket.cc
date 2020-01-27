// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/socket.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <netinet/in.h>
#include <poll.h>

#include <safemath/safe_conversions.h>

#include "private-socket.h"

namespace fio = ::llcpp::fuchsia::io;
namespace fsocket = ::llcpp::fuchsia::posix::socket;

namespace {
zx_status_t base_bind(zx::unowned_channel channel, const struct sockaddr* addr, socklen_t addrlen,
                      int16_t* out_code) {
  auto response = fsocket::BaseSocket::Call::Bind(
      std::move(channel),
      fidl::VectorView(reinterpret_cast<uint8_t*>(const_cast<sockaddr*>(addr)), addrlen));
  zx_status_t status = response.status();
  if (status != ZX_OK) {
    return status;
  }
  auto const& result = response.Unwrap()->result;
  if (result.is_err()) {
    *out_code = static_cast<int16_t>(result.err());
    return ZX_OK;
  }
  *out_code = 0;
  return ZX_OK;
}

zx_status_t base_connect(zx::unowned_channel channel, const struct sockaddr* addr,
                         socklen_t addrlen, int16_t* out_code) {
  auto response = fsocket::BaseSocket::Call::Connect(
      std::move(channel),
      fidl::VectorView(reinterpret_cast<uint8_t*>(const_cast<sockaddr*>(addr)), addrlen));
  zx_status_t status = response.status();
  if (status != ZX_OK) {
    return status;
  }
  auto const& result = response.Unwrap()->result;
  if (result.is_err()) {
    *out_code = static_cast<int16_t>(result.err());
    return ZX_OK;
  }
  *out_code = 0;
  return ZX_OK;
}

template <typename R>
zx_status_t base_getname(R response, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
  zx_status_t status = response.status();
  if (status != ZX_OK) {
    return status;
  }
  auto const& result = response.Unwrap()->result;
  if (result.is_err()) {
    *out_code = static_cast<int16_t>(result.err());
    return ZX_OK;
  }
  if (addrlen == nullptr || (*addrlen != 0 && addr == nullptr)) {
    *out_code = EFAULT;
    return ZX_OK;
  }
  *out_code = 0;
  auto const& out = result.response().addr;
  memcpy(addr, out.data(), std::min(static_cast<size_t>(*addrlen), out.count()));
  *addrlen = static_cast<socklen_t>(out.count());
  return ZX_OK;
}

zx_status_t base_getsockname(zx::unowned_channel channel, struct sockaddr* addr, socklen_t* addrlen,
                             int16_t* out_code) {
  return base_getname(fsocket::BaseSocket::Call::GetSockName(std::move(channel)), addr, addrlen,
                      out_code);
}

zx_status_t base_getpeername(zx::unowned_channel channel, struct sockaddr* addr, socklen_t* addrlen,
                             int16_t* out_code) {
  return base_getname(fsocket::BaseSocket::Call::GetPeerName(std::move(channel)), addr, addrlen,
                      out_code);
}

void getsockopt_inner(const fidl::VectorView<uint8_t>& fidl_optval, int level, int optname,
                      void* optval, socklen_t* optlen, int16_t* out_code) {
  size_t copy_len = std::min(static_cast<size_t>(*optlen), fidl_optval.count());
  bool do_optlen_check = true;
  // The following code block is to just keep up with Linux parity.
  switch (level) {
    case IPPROTO_IP:
      switch (optname) {
        case IP_TOS:
          // On Linux, when the optlen is < sizeof(int), only a single byte is
          // copied. As the TOS size is just a byte value, we are not losing
          // any information here.
          //
          // Note that this probably won't work right on big-endian systems.
          if (*optlen > 0 && *optlen < sizeof(int)) {
            copy_len = 1;
          }
          do_optlen_check = false;
          break;
        default:
          break;
      }
      break;
    case IPPROTO_IPV6:
      switch (optname) {
        case IPV6_TCLASS:
          do_optlen_check = false;
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }
  if (do_optlen_check) {
    if (fidl_optval.count() > *optlen) {
      *out_code = EINVAL;
      return;
    }
  }
  memcpy(optval, fidl_optval.data(), copy_len);
  *optlen = static_cast<socklen_t>(copy_len);
}

zx_status_t base_getsockopt(zx::unowned_channel channel, int level, int optname, void* optval,
                            socklen_t* optlen, int16_t* out_code) {
  auto response = fsocket::BaseSocket::Call::GetSockOpt(
      std::move(channel), static_cast<int16_t>(level), static_cast<int16_t>(optname));
  zx_status_t status = response.status();
  if (status != ZX_OK) {
    return status;
  }
  auto const& result = response.Unwrap()->result;
  if (result.is_err()) {
    *out_code = static_cast<int16_t>(result.err());
    return ZX_OK;
  }
  *out_code = 0;
  auto const& out = result.response().optval;
  getsockopt_inner(out, level, optname, optval, optlen, out_code);
  return ZX_OK;
}

zx_status_t base_setsockopt(zx::unowned_channel channel, int level, int optname, const void* optval,
                            socklen_t optlen, int16_t* out_code) {
  auto response = fsocket::BaseSocket::Call::SetSockOpt(
      std::move(channel), static_cast<int16_t>(level), static_cast<int16_t>(optname),
      fidl::VectorView(static_cast<uint8_t*>(const_cast<void*>(optval)), optlen));
  zx_status_t status = response.status();
  if (status != ZX_OK) {
    return status;
  }
  auto const& result = response.Unwrap()->result;
  if (result.is_err()) {
    *out_code = static_cast<int16_t>(result.err());
    return ZX_OK;
  }
  *out_code = 0;
  return ZX_OK;
}
}  // namespace

static zx_status_t zxsio_recvmsg_stream(fdio_t* io, struct msghdr* msg, int flags,
                                        size_t* out_actual, int16_t* out_code) {
  if (!(*fdio_get_ioflag(io) & IOFLAG_SOCKET_CONNECTED)) {
    return ZX_ERR_NOT_CONNECTED;
  }
  return fdio_zxio_recvmsg(io, msg, flags, out_actual, out_code);
}

static zx_status_t zxsio_sendmsg_stream(fdio_t* io, const struct msghdr* msg, int flags,
                                        size_t* out_actual, int16_t* out_code) {
  // TODO: support flags and control messages
  if (!(*fdio_get_ioflag(io) & IOFLAG_SOCKET_CONNECTED)) {
    return ZX_ERR_NOT_CONNECTED;
  }
  return fdio_zxio_sendmsg(io, msg, flags, out_actual, out_code);
}

static void fdio_wait_begin_socket(const zx::socket& socket, uint32_t* ioflag, uint32_t events,
                                   zx_handle_t* handle, zx_signals_t* out_signals) {
  *handle = socket.get();
  // TODO: locking for flags/state
  if (*ioflag & IOFLAG_SOCKET_CONNECTING) {
    // check the connection state
    zx_signals_t observed;
    zx_status_t status =
        socket.wait_one(ZXSIO_SIGNAL_CONNECTED, zx::time::infinite_past(), &observed);
    if (status == ZX_OK || status == ZX_ERR_TIMED_OUT) {
      if (observed & ZXSIO_SIGNAL_CONNECTED) {
        *ioflag &= ~IOFLAG_SOCKET_CONNECTING;
        *ioflag |= IOFLAG_SOCKET_CONNECTED;
      }
    }
  }

  zx_signals_t signals = ZX_SOCKET_PEER_CLOSED;
  if (events & (POLLOUT | POLLHUP)) {
    signals |= ZX_SOCKET_WRITE_DISABLED;
  }
  if (events & (POLLIN | POLLRDHUP)) {
    signals |= ZX_SOCKET_PEER_WRITE_DISABLED;
  }

  if (*ioflag & IOFLAG_SOCKET_CONNECTED) {
    // Can't subscribe to ZX_SOCKET_WRITABLE unless we're connected; such a subscription would
    // immediately fire, since the socket buffer is almost certainly empty.
    if (events & POLLOUT) {
      signals |= ZX_SOCKET_WRITABLE;
    }
    // This is just here for symmetry with POLLOUT above.
    if (events & POLLIN) {
      signals |= ZX_SOCKET_READABLE;
    }
  } else {
    if (events & POLLOUT) {
      // signal when connect() operation is finished.
      signals |= ZXSIO_SIGNAL_OUTGOING;
    }
    if (events & POLLIN) {
      // signal when a listening socket gets an incoming connection.
      signals |= ZXSIO_SIGNAL_INCOMING;
    }
  }
  *out_signals = signals;
}

static void zxsio_wait_end_stream(fdio_t* io, zx_signals_t signals, uint32_t* out_events) {
  uint32_t* ioflag = fdio_get_ioflag(io);
  // check the connection state
  if (*ioflag & IOFLAG_SOCKET_CONNECTING) {
    if (signals & ZXSIO_SIGNAL_CONNECTED) {
      *ioflag &= ~IOFLAG_SOCKET_CONNECTING;
      *ioflag |= IOFLAG_SOCKET_CONNECTED;
    }
  }
  uint32_t events = 0;
  if (signals & ZX_SOCKET_PEER_CLOSED) {
    events |= POLLIN | POLLOUT | POLLERR | POLLHUP | POLLRDHUP;
  }
  if (signals & ZX_SOCKET_WRITE_DISABLED) {
    events |= POLLHUP | POLLOUT;
  }
  if (signals & ZX_SOCKET_PEER_WRITE_DISABLED) {
    events |= POLLRDHUP | POLLIN;
  }
  if (*ioflag & IOFLAG_SOCKET_CONNECTED) {
    if (signals & ZX_SOCKET_WRITABLE) {
      events |= POLLOUT;
    }
    if (signals & ZX_SOCKET_READABLE) {
      events |= POLLIN;
    }
  } else {
    if (signals & ZXSIO_SIGNAL_OUTGOING) {
      events |= POLLOUT;
    }
    if (signals & ZXSIO_SIGNAL_INCOMING) {
      events |= POLLIN;
    }
  }
  *out_events = events;
}

// A |zxio_t| backend that uses a fuchsia.posix.socket.DatagramSocket object.
typedef struct zxio_datagram_socket {
  zxio_t io;
  zx::eventpair event;
  ::llcpp::fuchsia::posix::socket::DatagramSocket::SyncClient client;
} zxio_datagram_socket_t;

static_assert(sizeof(zxio_datagram_socket_t) <= sizeof(zxio_storage_t),
              "zxio_datagram_socket_t must fit inside zxio_storage_t.");

static zx::unowned_channel fdio_datagram_socket_get_channel(fdio_t* io) {
  auto const sio = reinterpret_cast<zxio_datagram_socket_t*>(fdio_get_zxio(io));
  return sio->client.channel().borrow();
}

static fdio_ops_t fdio_datagram_socket_ops = {
    .close = fdio_zxio_close,
    .open = fdio_default_open,
    .clone = fdio_zxio_clone,
    .unwrap = fdio_zxio_unwrap,
    .borrow_channel =
        [](fdio_t* io, zx_handle_t* h) {
          *h = fdio_datagram_socket_get_channel(io)->get();
          return ZX_OK;
        },
    .wait_begin =
        [](fdio_t* io, uint32_t events, zx_handle_t* handle, zx_signals_t* out_signals) {
          auto const sio = reinterpret_cast<zxio_datagram_socket_t*>(fdio_get_zxio(io));
          *handle = sio->event.get();
          zx_signals_t signals = ZX_EVENTPAIR_PEER_CLOSED;
          if (events & POLLIN) {
            signals |= ZXSIO_SIGNAL_INCOMING | ZXSIO_SIGNAL_SHUTDOWN_READ;
          }
          if (events & POLLOUT) {
            signals |= ZXSIO_SIGNAL_OUTGOING | ZXSIO_SIGNAL_SHUTDOWN_WRITE;
          }
          if (events & POLLRDHUP) {
            signals |= ZXSIO_SIGNAL_SHUTDOWN_READ;
          }
          *out_signals = signals;
        },
    .wait_end =
        [](fdio_t* io, zx_signals_t signals, uint32_t* out_events) {
          uint32_t events = 0;
          if (signals &
              (ZX_EVENTPAIR_PEER_CLOSED | ZXSIO_SIGNAL_INCOMING | ZXSIO_SIGNAL_SHUTDOWN_READ)) {
            events |= POLLIN;
          }
          if (signals &
              (ZX_EVENTPAIR_PEER_CLOSED | ZXSIO_SIGNAL_OUTGOING | ZXSIO_SIGNAL_SHUTDOWN_WRITE)) {
            events |= POLLOUT;
          }
          if (signals & ZX_EVENTPAIR_PEER_CLOSED) {
            events |= POLLERR;
          }
          if (signals & (ZX_EVENTPAIR_PEER_CLOSED | ZXSIO_SIGNAL_SHUTDOWN_READ)) {
            events |= POLLRDHUP;
          }
          *out_events = events;
        },
    .posix_ioctl = fdio_default_posix_ioctl,  // not supported
    .get_vmo = fdio_default_get_vmo,
    .get_token = fdio_default_get_token,
    .get_attr = fdio_default_get_attr,
    .set_attr = fdio_default_set_attr,
    .convert_to_posix_mode = fdio_default_convert_to_posix_mode,
    .dirent_iterator_init = fdio_default_dirent_iterator_init,
    .dirent_iterator_next = fdio_default_dirent_iterator_next,
    .dirent_iterator_destroy = fdio_default_dirent_iterator_destroy,
    .unlink = fdio_default_unlink,
    .truncate = fdio_default_truncate,
    .rename = fdio_default_rename,
    .link = fdio_default_link,
    .get_flags = fdio_default_get_flags,
    .set_flags = fdio_default_set_flags,
    .bind =
        [](fdio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
          return base_bind(fdio_datagram_socket_get_channel(io), addr, addrlen, out_code);
        },
    .connect =
        [](fdio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
          return base_connect(fdio_datagram_socket_get_channel(io), addr, addrlen, out_code);
        },
    .listen = [](fdio_t* io, int backlog, int16_t* out_code) { return ZX_ERR_WRONG_TYPE; },
    .accept = [](fdio_t* io, int flags, zx_handle_t* out_handle,
                 int16_t* out_code) { return ZX_ERR_WRONG_TYPE; },
    .getsockname =
        [](fdio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
          return base_getsockname(fdio_datagram_socket_get_channel(io), addr, addrlen, out_code);
        },
    .getpeername =
        [](fdio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
          return base_getpeername(fdio_datagram_socket_get_channel(io), addr, addrlen, out_code);
        },
    .getsockopt =
        [](fdio_t* io, int level, int optname, void* optval, socklen_t* optlen, int16_t* out_code) {
          return base_getsockopt(fdio_datagram_socket_get_channel(io), level, optname, optval,
                                 optlen, out_code);
        },
    .setsockopt =
        [](fdio_t* io, int level, int optname, const void* optval, socklen_t optlen,
           int16_t* out_code) {
          return base_setsockopt(fdio_datagram_socket_get_channel(io), level, optname, optval,
                                 optlen, out_code);
        },
    .recvmsg =
        [](fdio_t* io, struct msghdr* msg, int flags, size_t* out_actual, int16_t* out_code) {
          auto const sio = reinterpret_cast<zxio_datagram_socket_t*>(fdio_get_zxio(io));

          size_t datalen = 0;
          for (int i = 0; i < msg->msg_iovlen; ++i) {
            datalen += msg->msg_iov[i].iov_len;
          }

          auto response = sio->client.RecvMsg(msg->msg_namelen, static_cast<uint32_t>(datalen),
                                              msg->msg_controllen, static_cast<int16_t>(flags));
          zx_status_t status = response.status();
          if (status != ZX_OK) {
            return status;
          }
          auto const& result = response.Unwrap()->result;
          if (result.is_err()) {
            *out_code = static_cast<int16_t>(result.err());
            return ZX_OK;
          }
          *out_code = 0;

          {
            auto const& out = result.response().addr;
            if (msg->msg_name != nullptr) {
              memcpy(msg->msg_name, out.data(),
                     std::min(static_cast<size_t>(msg->msg_namelen), out.count()));
            }
            msg->msg_namelen = static_cast<socklen_t>(out.count());
          }

          {
            auto const& out = result.response().data;

            const uint8_t* data = out.begin();
            size_t remaining = out.count();
            for (int i = 0; i < msg->msg_iovlen; ++i) {
              size_t actual = std::min(msg->msg_iov[i].iov_len, remaining);
              memcpy(msg->msg_iov[i].iov_base, data, actual);
              data += actual;
              remaining -= actual;
            }
            if (result.response().truncated != 0) {
              msg->msg_flags |= MSG_TRUNC;
            } else {
              msg->msg_flags &= ~MSG_TRUNC;
            }
            size_t actual = out.count() - remaining;
            if ((flags & MSG_TRUNC) != 0) {
              actual += result.response().truncated;
            }
            *out_actual = actual;
          }

          {
            auto const& out = result.response().control;
            if (msg->msg_control != nullptr) {
              memcpy(msg->msg_control, out.data(),
                     std::min(static_cast<size_t>(msg->msg_controllen), out.count()));
            }
            msg->msg_controllen = static_cast<socklen_t>(out.count());
          }
          return ZX_OK;
        },
    .sendmsg =
        [](fdio_t* io, const struct msghdr* msg, int flags, size_t* out_actual, int16_t* out_code) {
          auto const sio = reinterpret_cast<zxio_datagram_socket_t*>(fdio_get_zxio(io));

          size_t total = 0;
          for (int i = 0; i < msg->msg_iovlen; ++i) {
            total += msg->msg_iov[i].iov_len;
          }

          std::vector<fidl::VectorView<uint8_t>> data;
          data.reserve(msg->msg_iovlen);

          for (int i = 0; i < msg->msg_iovlen; ++i) {
            data.push_back(fidl::VectorView(reinterpret_cast<uint8_t*>(msg->msg_iov[i].iov_base),
                                            msg->msg_iov[i].iov_len));
          }

          auto response = sio->client.SendMsg(
              fidl::VectorView(static_cast<uint8_t*>(msg->msg_name), msg->msg_namelen),
              fidl::VectorView(data),
              fidl::VectorView(static_cast<uint8_t*>(msg->msg_control), msg->msg_controllen),
              static_cast<int16_t>(flags));
          zx_status_t status = response.status();
          if (status != ZX_OK) {
            return status;
          }
          auto const& result = response.Unwrap()->result;
          if (result.is_err()) {
            *out_code = static_cast<int16_t>(result.err());
            return ZX_OK;
          }
          *out_code = 0;
          *out_actual = result.response().len;
          return ZX_OK;
        },
    .shutdown =
        [](fdio_t* io, int how, int16_t* out_code) {
          auto const sio = reinterpret_cast<zxio_datagram_socket_t*>(fdio_get_zxio(io));
          auto response = sio->client.Shutdown(static_cast<int16_t>(how));
          zx_status_t status = response.status();
          if (status != ZX_OK) {
            return status;
          }
          auto const& result = response.Unwrap()->result;
          if (result.is_err()) {
            *out_code = static_cast<int16_t>(result.err());
            return ZX_OK;
          }
          *out_code = 0;
          return ZX_OK;
        },
};

static constexpr zxio_ops_t zxio_datagram_socket_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = [](zxio_t* io) {
    auto zs = reinterpret_cast<zxio_datagram_socket_t*>(io);
    auto response = zs->client.Close();
    zs->~zxio_datagram_socket_t();
    return response.status();
  };
  ops.release = [](zxio_t* io, zx_handle_t* out_handle) {
    auto zs = reinterpret_cast<zxio_datagram_socket_t*>(io);
    *out_handle = zs->client.mutable_channel()->release();
    zs->~zxio_datagram_socket_t();
    return ZX_OK;
  };
  ops.clone = [](zxio_t* io, zx_handle_t* out_handle) {
    auto zs = reinterpret_cast<zxio_datagram_socket_t*>(io);
    zx::channel local, remote;
    zx_status_t status = zx::channel::create(0, &local, &remote);
    if (status != ZX_OK) {
      return status;
    }
    status = zs->client.Clone(fio::CLONE_FLAG_SAME_RIGHTS, std::move(remote)).status();
    if (status != ZX_OK) {
      return status;
    }
    *out_handle = local.release();
    return ZX_OK;
  };
  return ops;
}();

fdio_t* fdio_datagram_socket_create(
    zx::eventpair event, llcpp::fuchsia::posix::socket::DatagramSocket::SyncClient client) {
  fdio_t* io = fdio_alloc(&fdio_datagram_socket_ops);
  if (io == nullptr) {
    return nullptr;
  }
  zxio_storage_t* storage = fdio_get_zxio_storage(io);
  auto zs = new (storage) zxio_datagram_socket_t{
      .io = storage->io,
      .event = std::move(event),
      .client = std::move(client),
  };
  zxio_init(&zs->io, &zxio_datagram_socket_ops);
  return io;
}

// A |zxio_t| backend that uses a fuchsia.posix.socket.StreamSocket object.
typedef struct zxio_stream_socket {
  zxio_pipe_t pipe;

  ::llcpp::fuchsia::posix::socket::StreamSocket::SyncClient client;
} zxio_stream_socket_t;

static_assert(sizeof(zxio_stream_socket_t) <= sizeof(zxio_storage_t),
              "zxio_stream_socket_t must fit inside zxio_storage_t.");

static zx::unowned_channel fdio_stream_socket_get_channel(fdio_t* io) {
  auto const sio = reinterpret_cast<zxio_stream_socket_t*>(fdio_get_zxio(io));
  return sio->client.channel().borrow();
}

static fdio_ops_t fdio_stream_socket_ops = {
    .close = fdio_zxio_close,
    .open = fdio_default_open,
    .clone = fdio_zxio_clone,
    .unwrap = fdio_zxio_unwrap,
    .borrow_channel =
        [](fdio_t* io, zx_handle_t* h) {
          *h = fdio_stream_socket_get_channel(io)->get();
          return ZX_OK;
        },
    .wait_begin =
        [](fdio_t* io, uint32_t events, zx_handle_t* handle, zx_signals_t* out_signals) {
          auto const sio = reinterpret_cast<zxio_stream_socket_t*>(fdio_get_zxio(io));
          fdio_wait_begin_socket(sio->pipe.socket, fdio_get_ioflag(io), events, handle,
                                 out_signals);
        },
    .wait_end = zxsio_wait_end_stream,
    .posix_ioctl =
        [](fdio_t* io, int request, va_list va) {
          auto const sio = reinterpret_cast<zxio_stream_socket_t*>(fdio_get_zxio(io));
          return fdio_zx_socket_posix_ioctl(sio->pipe.socket, request, va);
        },
    .get_vmo = fdio_default_get_vmo,
    .get_token = fdio_default_get_token,
    .get_attr = fdio_default_get_attr,
    .set_attr = fdio_default_set_attr,
    .convert_to_posix_mode = fdio_default_convert_to_posix_mode,
    .dirent_iterator_init = fdio_default_dirent_iterator_init,
    .dirent_iterator_next = fdio_default_dirent_iterator_next,
    .dirent_iterator_destroy = fdio_default_dirent_iterator_destroy,
    .unlink = fdio_default_unlink,
    .truncate = fdio_default_truncate,
    .rename = fdio_default_rename,
    .link = fdio_default_link,
    .get_flags = fdio_default_get_flags,
    .set_flags = fdio_default_set_flags,
    .bind =
        [](fdio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
          return base_bind(fdio_stream_socket_get_channel(io), addr, addrlen, out_code);
        },
    .connect =
        [](fdio_t* io, const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
          return base_connect(fdio_stream_socket_get_channel(io), addr, addrlen, out_code);
        },
    .listen =
        [](fdio_t* io, int backlog, int16_t* out_code) {
          auto const sio = reinterpret_cast<zxio_stream_socket_t*>(fdio_get_zxio(io));
          auto response = sio->client.Listen(safemath::saturated_cast<int16_t>(backlog));
          zx_status_t status = response.status();
          if (status != ZX_OK) {
            return status;
          }
          auto const& result = response.Unwrap()->result;
          if (result.is_err()) {
            *out_code = static_cast<int16_t>(result.err());
            return ZX_OK;
          }
          *out_code = 0;
          return ZX_OK;
        },
    .accept =
        [](fdio_t* io, int flags, zx_handle_t* out_handle, int16_t* out_code) {
          auto const sio = reinterpret_cast<zxio_stream_socket_t*>(fdio_get_zxio(io));
          auto response = sio->client.Accept(static_cast<int16_t>(flags));
          zx_status_t status = response.status();
          if (status != ZX_OK) {
            return status;
          }
          auto& result = response.Unwrap()->result;
          if (result.is_err()) {
            *out_code = static_cast<int16_t>(result.err());
            return ZX_OK;
          }
          *out_code = 0;
          *out_handle = result.mutable_response().s.release();
          return ZX_OK;
        },
    .getsockname =
        [](fdio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
          return base_getsockname(fdio_stream_socket_get_channel(io), addr, addrlen, out_code);
        },
    .getpeername =
        [](fdio_t* io, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
          return base_getpeername(fdio_stream_socket_get_channel(io), addr, addrlen, out_code);
        },
    .getsockopt =
        [](fdio_t* io, int level, int optname, void* optval, socklen_t* optlen, int16_t* out_code) {
          return base_getsockopt(fdio_stream_socket_get_channel(io), level, optname, optval, optlen,
                                 out_code);
        },
    .setsockopt =
        [](fdio_t* io, int level, int optname, const void* optval, socklen_t optlen,
           int16_t* out_code) {
          return base_setsockopt(fdio_stream_socket_get_channel(io), level, optname, optval, optlen,
                                 out_code);
        },
    .recvmsg = zxsio_recvmsg_stream,
    .sendmsg = zxsio_sendmsg_stream,
    .shutdown =
        [](fdio_t* io, int how, int16_t* out_code) {
          if (!(*fdio_get_ioflag(io) & IOFLAG_SOCKET_CONNECTED)) {
            return ZX_ERR_BAD_STATE;
          }
          *out_code = 0;
          auto const sio = reinterpret_cast<zxio_stream_socket_t*>(fdio_get_zxio(io));
          return fdio_zx_socket_shutdown(sio->pipe.socket, how);
        },
};

static constexpr zxio_ops_t zxio_stream_socket_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = [](zxio_t* io) {
    auto zs = reinterpret_cast<zxio_stream_socket_t*>(io);
    // N.B. we don't call zs->control.Close() because such a call would block
    // until all bytes are drained zs->pipe.socket. Closing the channel (via the
    // destructor) is semantically equivalent and doesn't block.
    //
    // These semantics are not quite in accordance with POSIX, but this is the
    // best we can do given the double buffering inherent in the use of a zircon
    // socket as the transport. In the case of us backing a blocking socket, we
    // might want to block here, but the knowledge of blocking-or-not is not
    // available in this context, and the consequence of this deviation is judged
    // (by me - tamird@) to be minor.
    zs->~zxio_stream_socket_t();
    return ZX_OK;
  };
  ops.release = [](zxio_t* io, zx_handle_t* out_handle) {
    auto zs = reinterpret_cast<zxio_stream_socket_t*>(io);
    *out_handle = zs->client.mutable_channel()->release();
    zs->~zxio_stream_socket_t();
    return ZX_OK;
  };
  ops.clone = [](zxio_t* io, zx_handle_t* out_handle) {
    auto zs = reinterpret_cast<zxio_stream_socket_t*>(io);
    zx::channel local, remote;
    zx_status_t status = zx::channel::create(0, &local, &remote);
    if (status != ZX_OK) {
      return status;
    }
    status = zs->client.Clone(fio::CLONE_FLAG_SAME_RIGHTS, std::move(remote)).status();
    if (status != ZX_OK) {
      return status;
    }
    *out_handle = local.release();
    return ZX_OK;
  };
  ops.read_vector = zxio_stream_pipe_read_vector;
  ops.write_vector = zxio_stream_pipe_write_vector;
  return ops;
}();

fdio_t* fdio_stream_socket_create(zx::socket socket,
                                  llcpp::fuchsia::posix::socket::StreamSocket::SyncClient client) {
  fdio_t* io = fdio_alloc(&fdio_stream_socket_ops);
  if (io == nullptr) {
    return nullptr;
  }
  zxio_storage_t* storage = fdio_get_zxio_storage(io);
  auto zs = new (storage) zxio_stream_socket_t{
      .pipe =
          {
              .io = storage->io,
              .socket = std::move(socket),
          },
      .client = std::move(client),
  };
  zxio_init(&zs->pipe.io, &zxio_stream_socket_ops);
  return io;
}

bool fdio_is_socket(fdio_t* io) {
  if (!io) {
    return false;
  }
  const fdio_ops_t* ops = fdio_get_ops(io);
  return ops == &fdio_datagram_socket_ops || ops == &fdio_stream_socket_ops;
}

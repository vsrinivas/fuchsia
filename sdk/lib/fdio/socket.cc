// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zx/socket.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>

#include <safemath/safe_conversions.h>

#include "private-socket.h"

namespace fio = ::llcpp::fuchsia::io;
namespace fsocket = ::llcpp::fuchsia::posix::socket;
namespace fnet = ::llcpp::fuchsia::net;

namespace {

// A helper structure to keep a socket address and the variants allocations in stack.
struct SocketAddress {
  fnet::SocketAddress address;
  union U {
    fnet::Ipv4SocketAddress ipv4;
    fnet::Ipv6SocketAddress ipv6;

    U() { memset(this, 0x00, sizeof(U)); }
  } storage;

  zx_status_t LoadSockAddr(const struct sockaddr* addr, size_t addr_len) {
    // Address length larger than sockaddr_storage causes an error for API compatibility only.
    if (addr == nullptr || addr_len > sizeof(struct sockaddr_storage)) {
      return ZX_ERR_INVALID_ARGS;
    }
    switch (addr->sa_family) {
      case AF_INET: {
        if (addr_len < sizeof(struct sockaddr_in)) {
          return ZX_ERR_INVALID_ARGS;
        }
        const auto* s = reinterpret_cast<const struct sockaddr_in*>(addr);
        address.set_ipv4(fidl::unowned_ptr(&storage.ipv4));
        std::copy_n(reinterpret_cast<const uint8_t*>(&s->sin_addr.s_addr),
                    decltype(storage.ipv4.address.addr)::size(), storage.ipv4.address.addr.begin());
        storage.ipv4.port = ntohs(s->sin_port);
        return ZX_OK;
      }
      case AF_INET6: {
        if (addr_len < sizeof(struct sockaddr_in6)) {
          return ZX_ERR_INVALID_ARGS;
        }
        const auto* s = reinterpret_cast<const struct sockaddr_in6*>(addr);
        address.set_ipv6(fidl::unowned_ptr(&storage.ipv6));
        std::copy(std::begin(s->sin6_addr.s6_addr), std::end(s->sin6_addr.s6_addr),
                  storage.ipv6.address.addr.begin());
        storage.ipv6.port = ntohs(s->sin6_port);
        storage.ipv6.zone_index = s->sin6_scope_id;
        return ZX_OK;
      }
      default:
        return ZX_ERR_INVALID_ARGS;
    }
  }
};

fsocket::RecvMsgFlags to_recvmsg_flags(int flags) {
  fsocket::RecvMsgFlags r;
  if (flags & MSG_PEEK) {
    r |= fsocket::RecvMsgFlags::PEEK;
  }
  return r;
}

fsocket::SendMsgFlags to_sendmsg_flags(int flags) { return fsocket::SendMsgFlags(); }

size_t fidl_to_sockaddr(const fnet::SocketAddress& fidl, struct sockaddr* addr, size_t addr_len) {
  switch (fidl.which()) {
    case fnet::SocketAddress::Tag::kIpv4: {
      struct sockaddr_in tmp;
      auto* s = reinterpret_cast<struct sockaddr_in*>(addr);
      if (addr_len < sizeof(tmp)) {
        s = &tmp;
      }
      memset(s, 0x00, addr_len);
      const auto& ipv4 = fidl.ipv4();
      s->sin_family = AF_INET;
      s->sin_port = htons(ipv4.port);
      std::copy(ipv4.address.addr.begin(), ipv4.address.addr.end(),
                reinterpret_cast<uint8_t*>(&s->sin_addr));
      // Copy truncated address.
      if (s == &tmp) {
        memcpy(addr, &tmp, addr_len);
      }
      return sizeof(tmp);
    }
    case fnet::SocketAddress::Tag::kIpv6: {
      struct sockaddr_in6 tmp;
      auto* s = reinterpret_cast<struct sockaddr_in6*>(addr);
      if (addr_len < sizeof(tmp)) {
        s = &tmp;
      }
      memset(s, 0x00, addr_len);
      const auto& ipv6 = fidl.ipv6();
      s->sin6_family = AF_INET6;
      s->sin6_port = htons(ipv6.port);
      s->sin6_scope_id = static_cast<uint32_t>(ipv6.zone_index);
      std::copy(ipv6.address.addr.begin(), ipv6.address.addr.end(),
                s->sin6_addr.__in6_union.__s6_addr);
      // Copy truncated address.
      if (s == &tmp) {
        memcpy(addr, &tmp, addr_len);
      }
      return sizeof(tmp);
    }
  }
}

zx_status_t base_close(const zx::channel& channel) {
  auto response = fsocket::BaseSocket::Call::Close(channel.borrow());
  zx_status_t status;
  if ((status = response.status()) != ZX_OK) {
    return status;
  }
  if ((status = response->s) != ZX_OK) {
    return status;
  };
  if ((status = channel.wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(), nullptr)) != ZX_OK) {
    return status;
  }
  return ZX_OK;
}

zx_status_t base_bind(zx::unowned_channel channel, const struct sockaddr* addr, socklen_t addrlen,
                      int16_t* out_code) {
  SocketAddress fidl_addr;
  zx_status_t status = fidl_addr.LoadSockAddr(addr, addrlen);
  if (status != ZX_OK) {
    return status;
  }

  auto response =
      fsocket::BaseSocket::Call::Bind2(std::move(channel), std::move(fidl_addr.address));
  status = response.status();
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
  // If address is AF_UNSPEC we should call disconnect.
  if (addr->sa_family == AF_UNSPEC) {
    auto response = fsocket::BaseSocket::Call::Disconnect(std::move(channel));
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    const auto& result = response.Unwrap()->result;
    if (result.is_err()) {
      *out_code = static_cast<int16_t>(result.err());
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

  auto response =
      fsocket::BaseSocket::Call::Connect2(std::move(channel), std::move(fidl_addr.address));
  status = response.status();
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
  *addrlen = fidl_to_sockaddr(out, addr, *addrlen);
  return ZX_OK;
}

zx_status_t base_getsockname(zx::unowned_channel channel, struct sockaddr* addr, socklen_t* addrlen,
                             int16_t* out_code) {
  return base_getname(fsocket::BaseSocket::Call::GetSockName2(std::move(channel)), addr, addrlen,
                      out_code);
}

zx_status_t base_getpeername(zx::unowned_channel channel, struct sockaddr* addr, socklen_t* addrlen,
                             int16_t* out_code) {
  return base_getname(fsocket::BaseSocket::Call::GetPeerName2(std::move(channel)), addr, addrlen,
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
    case IPPROTO_TCP:
      switch (optname) {
        case TCP_CONGESTION:
          do_optlen_check = false;
          break;
        default:
          break;
      }
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
      fidl::VectorView(fidl::unowned_ptr(static_cast<uint8_t*>(const_cast<void*>(optval))),
                       optlen));
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

zx_status_t zxsio_posix_ioctl(fdio_t* io, int req, va_list va,
                              zx_status_t (*fallback)(fdio_t* io, int req, va_list va)) {
  switch (req) {
    case SIOCGIFNAME: {
      fsocket::Provider::SyncClient* provider;
      zx_status_t status = fdio_get_socket_provider(&provider);
      if (status != ZX_OK) {
        return status;
      }
      struct ifreq* ifr = va_arg(va, struct ifreq*);
      auto response = provider->InterfaceIndexToName(static_cast<uint64_t>(ifr->ifr_ifindex));
      status = response.status();
      if (status != ZX_OK) {
        return status;
      }
      auto const& result = response.Unwrap()->result;
      if (result.is_err()) {
        return result.err();
      }
      auto const& name = result.response().name;
      const size_t n = std::min(name.size(), sizeof(ifr->ifr_name));
      memcpy(ifr->ifr_name, name.data(), n);
      ifr->ifr_name[n] = 0;
      return ZX_OK;
    }
    case SIOCGIFINDEX: {
      fsocket::Provider::SyncClient* provider;
      zx_status_t status = fdio_get_socket_provider(&provider);
      if (status != ZX_OK) {
        return status;
      }
      struct ifreq* ifr = va_arg(va, struct ifreq*);
      fidl::StringView name(ifr->ifr_name, strnlen(ifr->ifr_name, sizeof(ifr->ifr_name) - 1));
      auto response = provider->InterfaceNameToIndex(std::move(name));
      status = response.status();
      if (status != ZX_OK) {
        if (status == ZX_ERR_INVALID_ARGS) {
          status = ZX_ERR_NOT_FOUND;
        }
        return status;
      }
      auto const& result = response.Unwrap()->result;
      if (result.is_err()) {
        return result.err();
      }
      ifr->ifr_ifindex = static_cast<int>(result.response().index);
      return ZX_OK;
    }
    default:
      return fallback(io, req, va);
  }
}

}  // namespace

static zx_status_t zxsio_recvmsg_stream(fdio_t* io, struct msghdr* msg, int flags,
                                        size_t* out_actual, int16_t* out_code) {
  if (!(*fdio_get_ioflag(io) & (IOFLAG_SOCKET_CONNECTING | IOFLAG_SOCKET_CONNECTED))) {
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

static void fdio_wait_begin_socket(fdio_t* io, const zx::socket& socket, uint32_t* ioflag,
                                   uint32_t events, zx_handle_t* handle,
                                   zx_signals_t* out_signals) {
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

  zxio_signals_t signals = ZXIO_SIGNAL_PEER_CLOSED;
  if (events & (POLLOUT | POLLHUP)) {
    signals |= ZXIO_SIGNAL_WRITE_DISABLED;
  }
  if (events & (POLLIN | POLLRDHUP)) {
    signals |= ZXIO_SIGNAL_READ_DISABLED;
  }

  if (*ioflag & IOFLAG_SOCKET_CONNECTED) {
    // Can't subscribe to ZX_SOCKET_WRITABLE unless we're connected; such a subscription would
    // immediately fire, since the socket buffer is almost certainly empty.
    if (events & POLLOUT) {
      signals |= ZXIO_SIGNAL_WRITABLE;
    }
  }

  if (*ioflag & (IOFLAG_SOCKET_CONNECTING | IOFLAG_SOCKET_CONNECTED)) {
    if (events & POLLIN) {
      signals |= ZXIO_SIGNAL_READABLE;
    }
  }

  zx_signals_t zx_signals = ZX_SIGNAL_NONE;
  zxio_wait_begin(fdio_get_zxio(io), signals, handle, &zx_signals);

  if (!(*ioflag & IOFLAG_SOCKET_CONNECTED)) {
    if (events & POLLOUT) {
      // signal when connect() operation is finished.
      zx_signals |= ZXSIO_SIGNAL_OUTGOING;
    }
    if (events & POLLIN) {
      // signal when a listening socket gets an incoming connection.
      zx_signals |= ZXSIO_SIGNAL_INCOMING;
    }
  }
  *out_signals = zx_signals;
}

static void zxsio_wait_end_stream(fdio_t* io, zx_signals_t zx_signals, uint32_t* out_events) {
  uint32_t* ioflag = fdio_get_ioflag(io);
  // check the connection state
  if (*ioflag & IOFLAG_SOCKET_CONNECTING) {
    if (zx_signals & ZXSIO_SIGNAL_CONNECTED) {
      *ioflag &= ~IOFLAG_SOCKET_CONNECTING;
      *ioflag |= IOFLAG_SOCKET_CONNECTED;
    }
    zx_signals &= ~ZXSIO_SIGNAL_CONNECTED;
  }

  zxio_signals_t signals = ZXIO_SIGNAL_NONE;
  zxio_wait_end(fdio_get_zxio(io), zx_signals, &signals);

  uint32_t events = 0;
  if (signals & ZXIO_SIGNAL_PEER_CLOSED) {
    events |= POLLIN | POLLOUT | POLLERR | POLLHUP | POLLRDHUP;
  }
  if (signals & ZXIO_SIGNAL_WRITE_DISABLED) {
    events |= POLLHUP | POLLOUT;
  }
  if (signals & ZXIO_SIGNAL_READ_DISABLED) {
    events |= POLLRDHUP | POLLIN;
  }
  if (*ioflag & IOFLAG_SOCKET_CONNECTED) {
    if (signals & ZXIO_SIGNAL_WRITABLE) {
      events |= POLLOUT;
    }
    if (signals & ZXIO_SIGNAL_READABLE) {
      events |= POLLIN;
    }
  } else {
    if (zx_signals & ZXSIO_SIGNAL_OUTGOING) {
      events |= POLLOUT;
    }
    if (zx_signals & ZXSIO_SIGNAL_INCOMING) {
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
    .posix_ioctl =
        [](fdio_t* io, int req, va_list va) {
          return zxsio_posix_ioctl(io, req, va, fdio_default_posix_ioctl);
        },
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
    .accept = [](fdio_t* io, int flags, struct sockaddr* addr, socklen_t* addrlen,
                 zx_handle_t* out_handle, int16_t* out_code) { return ZX_ERR_WRONG_TYPE; },
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

          bool want_addr = msg->msg_namelen != 0 && msg->msg_name != nullptr;
          auto response = sio->client.RecvMsg2(want_addr, static_cast<uint32_t>(datalen), false,
                                               to_recvmsg_flags(flags));
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
            // Result address has invalid tag when it's not provided by the server (when want_addr
            // is false).
            // TODO(fxbug.dev/58503): Use better representation of nullable union when available.
            if (want_addr && !out.has_invalid_tag()) {
              msg->msg_namelen = static_cast<socklen_t>(fidl_to_sockaddr(
                  out, static_cast<struct sockaddr*>(msg->msg_name), msg->msg_namelen));
            }
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
          // TODO(fxbug.dev/21106): Support control messages.
          msg->msg_controllen = 0;

          return ZX_OK;
        },
    .sendmsg =
        [](fdio_t* io, const struct msghdr* msg, int flags, size_t* out_actual, int16_t* out_code) {
          auto const sio = reinterpret_cast<zxio_datagram_socket_t*>(fdio_get_zxio(io));

          std::vector<uint8_t> data;
          auto vec = [&msg, &data]() -> fidl::VectorView<uint8_t> {
            switch (msg->msg_iovlen) {
              case 0: {
                return fidl::VectorView<uint8_t>();
              }
              case 1: {
                auto const& iov = msg->msg_iov[0];
                return fidl::VectorView(fidl::unowned_ptr(static_cast<uint8_t*>(iov.iov_base)),
                                        iov.iov_len);
              }
              default: {
                size_t total = 0;
                for (int i = 0; i < msg->msg_iovlen; ++i) {
                  total += msg->msg_iov[i].iov_len;
                }
                // TODO(abarth): avoid this copy.
                data.reserve(total);
                for (int i = 0; i < msg->msg_iovlen; ++i) {
                  auto const& iov = msg->msg_iov[i];
                  std::copy_n(static_cast<const uint8_t*>(iov.iov_base), iov.iov_len,
                              std::back_inserter(data));
                }
                return fidl::unowned_vec(data);
              }
            }
          };
          SocketAddress addr;
          // Attempt to load socket address if either name or namelen is set.
          // If only one is set, it'll result in INVALID_ARGS.
          if (msg->msg_namelen != 0 || msg->msg_name != nullptr) {
            zx_status_t status =
                addr.LoadSockAddr(static_cast<struct sockaddr*>(msg->msg_name), msg->msg_namelen);
            if (status != ZX_OK) {
              return status;
            }
          }
          // TODO(fxbug.dev/21106): Support control messages.
          // TODO(fxbug.dev/58503): Use better representation of nullable union when available.
          // Currently just using a default-initialized union with an invalid tag.
          auto response = sio->client.SendMsg(std::move(addr.address), vec(),
                                              fsocket::SendControlData(), to_sendmsg_flags(flags));
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
          fsocket::ShutdownMode mode;
          switch (how) {
            case SHUT_RD:
              mode = fsocket::ShutdownMode::READ;
              break;
            case SHUT_WR:
              mode = fsocket::ShutdownMode::WRITE;
              break;
            case SHUT_RDWR:
              mode = fsocket::ShutdownMode::READ | fsocket::ShutdownMode::WRITE;
              break;
            default:
              return ZX_ERR_INVALID_ARGS;
          }
          auto const sio = reinterpret_cast<zxio_datagram_socket_t*>(fdio_get_zxio(io));
          auto response = sio->client.Shutdown2(mode);
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
    zx_status_t channel_status = base_close(zs->client.channel());
    zs->~zxio_datagram_socket_t();
    return channel_status;
  };
  ops.release = [](zxio_t* io, zx_handle_t* out_handle) {
    auto zs = reinterpret_cast<zxio_datagram_socket_t*>(io);
    *out_handle = zs->client.mutable_channel()->release();
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
  zxio_t io;

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
          fdio_wait_begin_socket(io, sio->pipe.socket, fdio_get_ioflag(io), events, handle,
                                 out_signals);
        },
    .wait_end = zxsio_wait_end_stream,
    .posix_ioctl =
        [](fdio_t* io, int req, va_list va) {
          return zxsio_posix_ioctl(io, req, va, [](fdio_t* io, int req, va_list va) {
            auto const sio = reinterpret_cast<zxio_stream_socket_t*>(fdio_get_zxio(io));
            return fdio_zx_socket_posix_ioctl(sio->pipe.socket, req, va);
          });
        },
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
        [](fdio_t* io, int flags, struct sockaddr* addr, socklen_t* addrlen,
           zx_handle_t* out_handle, int16_t* out_code) {
          auto const sio = reinterpret_cast<zxio_stream_socket_t*>(fdio_get_zxio(io));
          bool want_addr = addr != nullptr && addrlen != nullptr;
          auto response = sio->client.Accept(want_addr);
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
          auto const& out = result.response().addr;
          // Result address has invalid tag when it's not provided by the server (when want_addr
          // is false).
          // TODO(fxbug.dev/58503): Use better representation of nullable union when available.
          if (want_addr && !out.has_invalid_tag()) {
            *addrlen = static_cast<socklen_t>(fidl_to_sockaddr(out, addr, *addrlen));
          }
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
          zx_signals_t observed;
          zx_status_t status = sio->pipe.socket.wait_one(ZX_SOCKET_PEER_CLOSED,
                                                         zx::time::infinite_past(), &observed);
          if (status == ZX_OK || status == ZX_ERR_TIMED_OUT) {
            if (observed & ZX_SOCKET_PEER_CLOSED) {
              return ZX_ERR_NOT_CONNECTED;
            }
            return fdio_zx_socket_shutdown(sio->pipe.socket, how);
          }
          return status;
        },
};

static constexpr zxio_ops_t zxio_stream_socket_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = [](zxio_t* io) {
    auto zs = reinterpret_cast<zxio_stream_socket_t*>(io);
    zx_status_t channel_status = base_close(zs->client.channel());
    zx_status_t aux_status = zxio_close(&zs->pipe.io);
    zxio_close(&zs->pipe.io);
    zs->~zxio_stream_socket_t();
    return channel_status != ZX_OK ? channel_status : aux_status;
  };
  ops.release = [](zxio_t* io, zx_handle_t* out_handle) {
    auto zs = reinterpret_cast<zxio_stream_socket_t*>(io);
    *out_handle = zs->client.mutable_channel()->release();
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
  ops.wait_begin = [](zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                      zx_signals_t* out_zx_signals) {
    auto zs = reinterpret_cast<zxio_stream_socket_t*>(io);
    zxio_wait_begin(&zs->pipe.io, zxio_signals, out_handle, out_zx_signals);
  };
  ops.wait_end = [](zxio_t* io, zx_signals_t zx_signals, zxio_signals_t* out_zxio_signals) {
    auto zs = reinterpret_cast<zxio_stream_socket_t*>(io);
    zxio_wait_end(&zs->pipe.io, zx_signals, out_zxio_signals);
  };
  ops.readv = [](zxio_t* io, const zx_iovec_t* vector, size_t vector_count, zxio_flags_t flags,
                 size_t* out_actual) {
    auto zs = reinterpret_cast<zxio_stream_socket_t*>(io);
    return zxio_readv(&zs->pipe.io, vector, vector_count, flags, out_actual);
  };
  ops.writev = [](zxio_t* io, const zx_iovec_t* vector, size_t vector_count, zxio_flags_t flags,
                  size_t* out_actual) {
    auto zs = reinterpret_cast<zxio_stream_socket_t*>(io);
    return zxio_writev(&zs->pipe.io, vector, vector_count, flags, out_actual);
  };
  return ops;
}();

fdio_t* fdio_stream_socket_create(zx::socket socket,
                                  llcpp::fuchsia::posix::socket::StreamSocket::SyncClient client,
                                  zx_info_socket_t info) {
  fdio_t* io = fdio_alloc(&fdio_stream_socket_ops);
  if (io == nullptr) {
    return nullptr;
  }
  zxio_storage_t* storage = fdio_get_zxio_storage(io);
  auto zs = new (storage) zxio_stream_socket_t{
      .io = {},
      .pipe = {},
      .client = std::move(client),
  };
  zxio_init(&zs->io, &zxio_stream_socket_ops);
  zxio_pipe_init(reinterpret_cast<zxio_storage_t*>(&zs->pipe), std::move(socket), info);
  return io;
}

bool fdio_is_socket(fdio_t* io) {
  if (!io) {
    return false;
  }
  const fdio_ops_t* ops = fdio_get_ops(io);
  return ops == &fdio_datagram_socket_ops || ops == &fdio_stream_socket_ops;
}

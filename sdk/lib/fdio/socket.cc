// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fdio/fdio.h>
#include <lib/zx/socket.h>
#include <lib/zxio/inception.h>
#include <lib/zxio/null.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>

#include <vector>

#include <safemath/safe_conversions.h>

#include "fdio_unistd.h"
#include "private-socket.h"
#include "zxio.h"

namespace fio = fuchsia_io;
namespace fsocket = fuchsia_posix_socket;
namespace fnet = fuchsia_net;

namespace {

// A helper structure to keep a socket address and the variants allocations in stack.
struct SocketAddress {
  fnet::wire::SocketAddress address;
  union U {
    fnet::wire::Ipv4SocketAddress ipv4;
    fnet::wire::Ipv6SocketAddress ipv6;

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
        address.set_ipv4(
            fidl::ObjectView<fnet::wire::Ipv4SocketAddress>::FromExternal(&storage.ipv4));
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
        address.set_ipv6(
            fidl::ObjectView<fnet::wire::Ipv6SocketAddress>::FromExternal(&storage.ipv6));
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

fsocket::wire::RecvMsgFlags to_recvmsg_flags(int flags) {
  fsocket::wire::RecvMsgFlags r;
  if (flags & MSG_PEEK) {
    r |= fsocket::wire::RecvMsgFlags::PEEK;
  }
  return r;
}

fsocket::wire::SendMsgFlags to_sendmsg_flags(int flags) { return fsocket::wire::SendMsgFlags(); }

socklen_t fidl_to_sockaddr(const fnet::wire::SocketAddress& fidl, struct sockaddr* addr,
                           socklen_t addr_len) {
  switch (fidl.which()) {
    case fnet::wire::SocketAddress::Tag::kIpv4: {
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
    case fnet::wire::SocketAddress::Tag::kIpv6: {
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

template <typename T,
          typename =
              std::enable_if_t<std::is_same_v<T, fidl::WireSyncClient<fsocket::DatagramSocket>> ||
                               std::is_same_v<T, fidl::WireSyncClient<fsocket::StreamSocket>>>>
struct BaseSocket {
  static_assert(std::is_same_v<T, fidl::WireSyncClient<fsocket::DatagramSocket>> ||
                std::is_same_v<T, fidl::WireSyncClient<fsocket::StreamSocket>>);

 public:
  explicit BaseSocket(T& client) : client_(client) {}

  T& client() { return client_; }

  zx_status_t clone(zx_handle_t* out_handle) {
    zx::status endpoints = fidl::CreateEndpoints<fio::Node>();
    if (endpoints.is_error()) {
      return endpoints.status_value();
    }
    zx_status_t status =
        client().Clone(fio::wire::CLONE_FLAG_SAME_RIGHTS, std::move(endpoints->server)).status();
    if (status != ZX_OK) {
      return status;
    }
    *out_handle = endpoints->client.channel().release();
    return ZX_OK;
  }

  zx_status_t close() {
    auto response = client().Close();
    zx_status_t status;
    if ((status = response.status()) != ZX_OK) {
      return status;
    }
    if ((status = response->s) != ZX_OK) {
      return status;
    }
    if ((status = client().channel().wait_one(ZX_CHANNEL_PEER_CLOSED, zx::time::infinite(),
                                              nullptr)) != ZX_OK) {
      return status;
    }
    return ZX_OK;
  }

  zx_status_t bind(const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    SocketAddress fidl_addr;
    zx_status_t status = fidl_addr.LoadSockAddr(addr, addrlen);
    if (status != ZX_OK) {
      return status;
    }

    auto response = client().Bind(fidl_addr.address);
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

  zx_status_t connect(const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) {
    // If address is AF_UNSPEC we should call disconnect.
    if (addr->sa_family == AF_UNSPEC) {
      auto response = client().Disconnect();
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

    auto response = client().Connect(fidl_addr.address);
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
  zx_status_t getname(R response, struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
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

  zx_status_t getsockname(struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return getname(client().GetSockName(), addr, addrlen, out_code);
  }

  zx_status_t getpeername(struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) {
    return getname(client().GetPeerName(), addr, addrlen, out_code);
  }

  void getsockopt_inner(const fidl::VectorView<uint8_t>& fidl_optval, int level, int optname,
                        void* optval, socklen_t* optlen, int16_t* out_code) {
    size_t copy_len = std::min(static_cast<size_t>(*optlen), fidl_optval.count());
    bool do_optlen_check = true;
    // The following code block is to just keep up with Linux parity.
    switch (level) {
      case SOL_IP:
        switch (optname) {
          case IP_TOS:
          case IP_RECVTOS:
          case IP_MULTICAST_TTL:
          case IP_MULTICAST_LOOP:
            // On Linux, when the optlen is < sizeof(int), only a single byte is
            // copied. As these options' value is just a single byte, we are not losing
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
      case SOL_IPV6:
        switch (optname) {
          case IPV6_MULTICAST_HOPS:
          case IPV6_MULTICAST_LOOP:
          case IPV6_RECVTCLASS:
          case IPV6_TCLASS:
            do_optlen_check = false;
            break;
          default:
            break;
        }
        break;
      case SOL_TCP:
        switch (optname) {
          case TCP_CONGESTION:
          case TCP_INFO:
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

  zx_status_t getsockopt(int level, int optname, void* optval, socklen_t* optlen,
                         int16_t* out_code) {
    auto response = client().GetSockOpt(static_cast<int16_t>(level), static_cast<int16_t>(optname));
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

  zx_status_t setsockopt(int level, int optname, const void* optval, socklen_t optlen,
                         int16_t* out_code) {
    auto response =
        client().SetSockOpt(static_cast<int16_t>(level), static_cast<int16_t>(optname),
                            fidl::VectorView<uint8_t>::FromExternal(
                                static_cast<uint8_t*>(const_cast<void*>(optval)), optlen));
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

 private:
  T& client_;
};

// Prevent divergence in flag bitmasks between libc and fuchsia.posix.socket FIDL library.
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::UP) == IFF_UP);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::BROADCAST) == IFF_BROADCAST);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::DEBUG) == IFF_DEBUG);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::LOOPBACK) == IFF_LOOPBACK);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::POINTTOPOINT) ==
              IFF_POINTOPOINT);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::NOTRAILERS) == IFF_NOTRAILERS);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::RUNNING) == IFF_RUNNING);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::NOARP) == IFF_NOARP);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::PROMISC) == IFF_PROMISC);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::ALLMULTI) == IFF_ALLMULTI);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::LEADER) == IFF_MASTER);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::FOLLOWER) == IFF_SLAVE);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::MULTICAST) == IFF_MULTICAST);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::PORTSEL) == IFF_PORTSEL);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::AUTOMEDIA) == IFF_AUTOMEDIA);
static_assert(static_cast<uint16_t>(fsocket::wire::InterfaceFlags::DYNAMIC) == IFF_DYNAMIC);

template <typename F>
Errno zxsio_posix_ioctl(int req, va_list va, F fallback) {
  switch (req) {
    case SIOCGIFNAME: {
      auto& provider = fdio_get_socket_provider();
      if (provider.is_error()) {
        return Errno(fdio_status_to_errno(provider.error_value()));
      }
      struct ifreq* ifr = va_arg(va, struct ifreq*);
      auto response = provider->InterfaceIndexToName(static_cast<uint64_t>(ifr->ifr_ifindex));
      zx_status_t status = response.status();
      if (status != ZX_OK) {
        return Errno(fdio_status_to_errno(status));
      }
      auto const& result = response.Unwrap()->result;
      if (result.is_err()) {
        if (result.err() == ZX_ERR_NOT_FOUND) {
          return Errno(ENODEV);
        }
        return Errno(fdio_status_to_errno(result.err()));
      }
      auto const& name = result.response().name;
      const size_t n = std::min(name.size(), sizeof(ifr->ifr_name));
      memcpy(ifr->ifr_name, name.data(), n);
      ifr->ifr_name[n] = 0;
      return Errno(Errno::Ok);
    }
    case SIOCGIFINDEX: {
      auto& provider = fdio_get_socket_provider();
      if (provider.is_error()) {
        return Errno(fdio_status_to_errno(provider.error_value()));
      }
      struct ifreq* ifr = va_arg(va, struct ifreq*);
      fidl::StringView name(ifr->ifr_name, strnlen(ifr->ifr_name, sizeof(ifr->ifr_name) - 1));
      auto response = provider->InterfaceNameToIndex(name);
      zx_status_t status = response.status();
      if (status != ZX_OK) {
        if (status == ZX_ERR_INVALID_ARGS) {
          // FIDL calls will return ZX_ERR_INVALID_ARGS if the passed string
          // (`name` in this case) fails UTF-8 validation.
          return Errno(ENODEV);
        }
        return Errno(fdio_status_to_errno(status));
      }
      auto const& result = response.Unwrap()->result;
      if (result.is_err()) {
        if (result.err() == ZX_ERR_NOT_FOUND) {
          return Errno(ENODEV);
        }
        return Errno(fdio_status_to_errno(result.err()));
      }
      ifr->ifr_ifindex = static_cast<int>(result.response().index);
      return Errno(Errno::Ok);
    }
    case SIOCGIFFLAGS: {
      auto& provider = fdio_get_socket_provider();
      if (provider.is_error()) {
        return Errno(fdio_status_to_errno(provider.error_value()));
      }
      struct ifreq* ifr = va_arg(va, struct ifreq*);
      fidl::StringView name(ifr->ifr_name, strnlen(ifr->ifr_name, sizeof(ifr->ifr_name) - 1));
      auto response = provider->InterfaceNameToFlags(name);
      zx_status_t status = response.status();
      if (status != ZX_OK) {
        if (status == ZX_ERR_INVALID_ARGS) {
          // FIDL calls will return ZX_ERR_INVALID_ARGS if the passed string
          // (`name` in this case) fails UTF-8 validation.
          return Errno(ENODEV);
        }
        return Errno(fdio_status_to_errno(status));
      }
      auto const& result = response.Unwrap()->result;
      if (result.is_err()) {
        if (result.err() == ZX_ERR_NOT_FOUND) {
          return Errno(ENODEV);
        }
        return Errno(fdio_status_to_errno(result.err()));
      }
      ifr->ifr_flags =
          static_cast<uint16_t>(result.response().flags);  // NOLINT(bugprone-narrowing-conversions)
      return Errno(Errno::Ok);
    }
    case SIOCGIFCONF: {
      struct ifconf* ifc_ptr = va_arg(va, struct ifconf*);
      if (ifc_ptr == nullptr) {
        return Errno(EFAULT);
      }
      struct ifconf& ifc = *ifc_ptr;

      auto& provider = fdio_get_socket_provider();
      if (provider.is_error()) {
        return Errno(fdio_status_to_errno(provider.error_value()));
      }
      auto response = provider->GetInterfaceAddresses();
      zx_status_t status = response.status();
      if (status != ZX_OK) {
        return Errno(fdio_status_to_errno(status));
      }
      const auto& interfaces = response.Unwrap()->interfaces;

      // If `ifc_req` is NULL, return the necessary buffer size in bytes for
      // receiving all available addresses in `ifc_len`.
      //
      // This allows the caller to determine the necessary buffer size
      // beforehand, and is the documented manual behavior.
      // See: https://man7.org/linux/man-pages/man7/netdevice.7.html
      if (ifc.ifc_req == nullptr) {
        int len = 0;
        for (const auto& iface : interfaces) {
          for (const auto& address : iface.addresses()) {
            if (address.addr.which() == fnet::wire::IpAddress::Tag::kIpv4) {
              len += sizeof(struct ifreq);
            }
          }
        }
        ifc.ifc_len = len;
        return Errno(Errno::Ok);
      }

      struct ifreq* ifr = ifc.ifc_req;
      const auto buffer_full = [&] {
        return ifr + 1 > ifc.ifc_req + ifc.ifc_len / sizeof(struct ifreq);
      };
      for (const auto& iface : interfaces) {
        // Don't write past the caller-allocated buffer.
        // C++ doesn't support break labels, so we check this in both the inner
        // and outer loops.
        if (buffer_full()) {
          break;
        }
        // This should not happen, and would indicate a protocol error with
        // fuchsia.posix.socket/Provider.GetInterfaceAddresses.
        if (!iface.has_name() || !iface.has_addresses()) {
          continue;
        }

        const auto& if_name = iface.name();
        for (const auto& address : iface.addresses()) {
          // Don't write past the caller-allocated buffer.
          if (buffer_full()) {
            break;
          }
          // SIOCGIFCONF only returns interface addresses of the AF_INET (IPv4)
          // family for compatibility; this is the behavior documented in the
          // manual. See: https://man7.org/linux/man-pages/man7/netdevice.7.html
          const auto& addr = address.addr;
          if (addr.which() != fnet::wire::IpAddress::Tag::kIpv4) {
            continue;
          }

          // Write interface name.
          size_t len = std::min(if_name.size(), sizeof(ifr->ifr_name) - 1);
          memcpy(ifr->ifr_name, if_name.data(), len);
          ifr->ifr_name[len] = 0;

          // Write interface address.
          auto* s = reinterpret_cast<struct sockaddr_in*>(&ifr->ifr_addr);
          const auto& ipv4 = addr.ipv4();
          s->sin_family = AF_INET;
          s->sin_port = 0;
          std::copy(ipv4.addr.begin(), ipv4.addr.end(), reinterpret_cast<uint8_t*>(&s->sin_addr));

          ifr++;
        }
      }
      ifc.ifc_len = static_cast<int>((ifr - ifc.ifc_req) * sizeof(struct ifreq));
      return Errno(Errno::Ok);
    }
    default:
      return fallback(req, va);
  }
}

}  // namespace

// A |zxio_t| backend that uses a fuchsia.posix.socket.DatagramSocket object.
using zxio_datagram_socket_t = struct zxio_datagram_socket {
  zxio_t io;
  zx::eventpair event;
  fidl::WireSyncClient<fsocket::DatagramSocket> client;
};

static_assert(sizeof(zxio_datagram_socket_t) <= sizeof(zxio_storage_t),
              "zxio_datagram_socket_t must fit inside zxio_storage_t.");

namespace fdio_internal {

struct datagram_socket : public zxio {
  zx_status_t borrow_channel(zx_handle_t* h) override {
    *h = zxio_datagram_socket().client.channel().get();
    return ZX_OK;
  }

  void wait_begin(uint32_t events, zx_handle_t* handle, zx_signals_t* out_signals) override {
    *handle = zxio_datagram_socket().event.get();
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
  }

  void wait_end(zx_signals_t signals, uint32_t* out_events) override {
    uint32_t events = 0;
    if (signals & (ZX_EVENTPAIR_PEER_CLOSED | ZXSIO_SIGNAL_INCOMING | ZXSIO_SIGNAL_SHUTDOWN_READ)) {
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
  }

  Errno posix_ioctl(int req, va_list va) override {
    return zxsio_posix_ioctl(req, va,
                             [this](int req, va_list va) { return base::posix_ioctl(req, va); });
  }

  zx_status_t bind(const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) override {
    return BaseSocket(zxio_datagram_socket().client).bind(addr, addrlen, out_code);
  }

  zx_status_t connect(const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) override {
    return BaseSocket(zxio_datagram_socket().client).connect(addr, addrlen, out_code);
  }

  zx_status_t listen(int backlog, int16_t* out_code) override { return ZX_ERR_WRONG_TYPE; }

  zx_status_t accept(int flags, struct sockaddr* addr, socklen_t* addrlen, zx_handle_t* out_handle,
                     int16_t* out_code) override {
    return ZX_ERR_WRONG_TYPE;
  }

  zx_status_t getsockname(struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) override {
    return BaseSocket(zxio_datagram_socket().client).getsockname(addr, addrlen, out_code);
  }

  zx_status_t getpeername(struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) override {
    return BaseSocket(zxio_datagram_socket().client).getpeername(addr, addrlen, out_code);
  }

  zx_status_t getsockopt(int level, int optname, void* optval, socklen_t* optlen,
                         int16_t* out_code) override {
    return BaseSocket(zxio_datagram_socket().client)
        .getsockopt(level, optname, optval, optlen, out_code);
  }

  zx_status_t setsockopt(int level, int optname, const void* optval, socklen_t optlen,
                         int16_t* out_code) override {
    return BaseSocket(zxio_datagram_socket().client)
        .setsockopt(level, optname, optval, optlen, out_code);
  }

  zx_status_t recvmsg(struct msghdr* msg, int flags, size_t* out_actual,
                      int16_t* out_code) override {
    auto& client = zxio_datagram_socket().client;

    size_t datalen = 0;
    for (int i = 0; i < msg->msg_iovlen; ++i) {
      datalen += msg->msg_iov[i].iov_len;
    }

    bool want_addr = msg->msg_namelen != 0 && msg->msg_name != nullptr;
    auto response =
        client.RecvMsg(want_addr, static_cast<uint32_t>(datalen), false, to_recvmsg_flags(flags));
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
        msg->msg_namelen = static_cast<socklen_t>(
            fidl_to_sockaddr(out, static_cast<struct sockaddr*>(msg->msg_name), msg->msg_namelen));
      }
    }

    {
      auto const& out = result.response().data;

      const uint8_t* data = out.begin();
      size_t remaining = out.count();
      for (int i = 0; remaining != 0 && i < msg->msg_iovlen; ++i) {
        auto const& iov = msg->msg_iov[i];
        if (iov.iov_base != nullptr) {
          size_t actual = std::min(iov.iov_len, remaining);
          memcpy(iov.iov_base, data, actual);
          data += actual;
          remaining -= actual;
        } else if (iov.iov_len != 0) {
          *out_code = EFAULT;
          return ZX_OK;
        }
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
  }

  zx_status_t sendmsg(const struct msghdr* msg, int flags, size_t* out_actual,
                      int16_t* out_code) override {
    auto& client = zxio_datagram_socket().client;

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

    size_t total = 0;
    for (int i = 0; i < msg->msg_iovlen; ++i) {
      auto const& iov = msg->msg_iov[i];
      if (iov.iov_base == nullptr && iov.iov_len != 0) {
        *out_code = EFAULT;
        return ZX_OK;
      }
      total += iov.iov_len;
    }

    std::vector<uint8_t> data;
    auto vec = fidl::VectorView<uint8_t>();
    switch (msg->msg_iovlen) {
      case 0: {
        break;
      }
      case 1: {
        auto const& iov = *msg->msg_iov;
        vec = fidl::VectorView<uint8_t>::FromExternal(static_cast<uint8_t*>(iov.iov_base),
                                                      iov.iov_len);
        break;
      }
      default: {
        // TODO(abarth): avoid this copy.
        data.reserve(total);
        for (int i = 0; i < msg->msg_iovlen; ++i) {
          auto const& iov = msg->msg_iov[i];
          std::copy_n(static_cast<const uint8_t*>(iov.iov_base), iov.iov_len,
                      std::back_inserter(data));
        }
        vec = fidl::VectorView<uint8_t>::FromExternal(data);
      }
    }
    // TODO(fxbug.dev/21106): Support control messages.
    // TODO(fxbug.dev/58503): Use better representation of nullable union when available.
    // Currently just using a default-initialized union with an invalid tag.
    auto response = client.SendMsg(addr.address, vec, fsocket::wire::SendControlData(),
                                   to_sendmsg_flags(flags));
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
  }

  zx_status_t shutdown(int how, int16_t* out_code) override {
    using fsocket::wire::ShutdownMode;
    ShutdownMode mode;
    switch (how) {
      case SHUT_RD:
        mode = ShutdownMode::READ;
        break;
      case SHUT_WR:
        mode = ShutdownMode::WRITE;
        break;
      case SHUT_RDWR:
        mode = ShutdownMode::READ | ShutdownMode::WRITE;
        break;
      default:
        return ZX_ERR_INVALID_ARGS;
    }
    auto response = zxio_datagram_socket().client.Shutdown(mode);
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

 protected:
  friend class fbl::internal::MakeRefCountedHelper<datagram_socket>;
  friend class fbl::RefPtr<datagram_socket>;

  datagram_socket() = default;
  ~datagram_socket() override = default;

 private:
  zxio_datagram_socket_t& zxio_datagram_socket() {
    return *reinterpret_cast<zxio_datagram_socket_t*>(&zxio_storage().io);
  }
};

}  // namespace fdio_internal

static constexpr zxio_ops_t zxio_datagram_socket_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = [](zxio_t* io) {
    auto zs = reinterpret_cast<zxio_datagram_socket_t*>(io);
    zx_status_t channel_status = BaseSocket(zs->client).close();
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
    return BaseSocket(zs->client).clone(out_handle);
  };
  return ops;
}();

fdio_ptr fdio_datagram_socket_create(zx::eventpair event,
                                     fidl::ClientEnd<fsocket::DatagramSocket> client) {
  fdio_ptr io = fbl::MakeRefCounted<fdio_internal::datagram_socket>();
  if (io == nullptr) {
    return nullptr;
  }
  zxio_storage_t& storage = io->zxio_storage();
  auto zs = new (&storage) zxio_datagram_socket_t{
      .io = storage.io,
      .event = std::move(event),
      .client = fidl::BindSyncClient(std::move(client)),
  };
  zxio_init(&zs->io, &zxio_datagram_socket_ops);
  return io;
}

// A |zxio_t| backend that uses a fuchsia.posix.socket.StreamSocket object.
using zxio_stream_socket_t = struct zxio_stream_socket {
  zxio_t io;

  zxio_pipe_t pipe;

  fidl::WireSyncClient<fsocket::StreamSocket> client;
};

static_assert(sizeof(zxio_stream_socket_t) <= sizeof(zxio_storage_t),
              "zxio_stream_socket_t must fit inside zxio_storage_t.");

namespace fdio_internal {

struct stream_socket : public pipe {
  zx_status_t borrow_channel(zx_handle_t* h) override {
    *h = zxio_stream_socket().client.channel().get();
    return ZX_OK;
  }

  void wait_begin(uint32_t events, zx_handle_t* handle, zx_signals_t* out_signals) override {
    // TODO(https://fxbug.dev/67465): locking for flags/state
    if (ioflag() & IOFLAG_SOCKET_CONNECTING) {
      // check the connection state
      zx_signals_t observed;
      zx_status_t status = zxio_stream_socket().pipe.socket.wait_one(
          ZXSIO_SIGNAL_CONNECTED, zx::time::infinite_past(), &observed);
      if (status == ZX_OK || status == ZX_ERR_TIMED_OUT) {
        if (observed & ZXSIO_SIGNAL_CONNECTED) {
          ioflag() = (ioflag() ^ IOFLAG_SOCKET_CONNECTING) | IOFLAG_SOCKET_CONNECTED;
        }
      }
    }

    // Stream sockets which are non-listening or unconnected do not have a potential peer
    // to generate any waitable signals, skip signal waiting and notify the caller of the
    // same.
    if (!(ioflag() &
          (IOFLAG_SOCKET_LISTENING | IOFLAG_SOCKET_CONNECTING | IOFLAG_SOCKET_CONNECTED))) {
      *out_signals = ZX_SIGNAL_NONE;
      return;
    }

    zxio_signals_t signals = ZXIO_SIGNAL_PEER_CLOSED;

    if (ioflag() & IOFLAG_SOCKET_CONNECTED) {
      return wait_begin_inner(events, signals, handle, out_signals);
    }

    if (events & POLLOUT) {
      signals |= ZXIO_SIGNAL_WRITE_DISABLED;
    }
    if (events & (POLLIN | POLLRDHUP)) {
      signals |= ZXIO_SIGNAL_READ_DISABLED;
    }

    if (ioflag() & IOFLAG_SOCKET_CONNECTING) {
      if (events & POLLIN) {
        signals |= ZXIO_SIGNAL_READABLE;
      }
    }

    zx_signals_t zx_signals = ZX_SIGNAL_NONE;
    zxio_wait_begin(&zxio_storage().io, signals, handle, &zx_signals);

    if (events & POLLOUT) {
      // signal when connect() operation is finished.
      zx_signals |= ZXSIO_SIGNAL_OUTGOING;
    }
    if (events & POLLIN) {
      // signal when a listening socket gets an incoming connection.
      zx_signals |= ZXSIO_SIGNAL_INCOMING;
    }
    *out_signals = zx_signals;
  }

  void wait_end(zx_signals_t zx_signals, uint32_t* out_events) override {
    // The caller has not provided any waitable signal, this is the case where we are asked to wait
    // on an unconnected or non-listening socket.
    if (zx_signals == ZX_SIGNAL_NONE) {
      *out_events = POLLOUT | POLLHUP;
      return;
    }

    // check the connection state
    if (ioflag() & IOFLAG_SOCKET_CONNECTING) {
      if (zx_signals & ZXSIO_SIGNAL_CONNECTED) {
        ioflag() = (ioflag() ^ IOFLAG_SOCKET_CONNECTING) | IOFLAG_SOCKET_CONNECTED;
      }
      zx_signals &= ~ZXSIO_SIGNAL_CONNECTED;
    }

    zxio_signals_t signals = ZXIO_SIGNAL_NONE;
    uint32_t events = 0;
    if (ioflag() & IOFLAG_SOCKET_CONNECTED) {
      wait_end_inner(zx_signals, &events, &signals);
    } else {
      zxio_wait_end(&zxio_storage().io, zx_signals, &signals);
      if (zx_signals & ZXSIO_SIGNAL_OUTGOING) {
        events |= POLLOUT;
      }
      if (zx_signals & ZXSIO_SIGNAL_INCOMING) {
        events |= POLLIN;
      }
    }

    if (signals & ZXIO_SIGNAL_PEER_CLOSED) {
      // Update flags to hold an error state which can be harvested by read/write calls.
      // For other errors like connection timeouts, no error is reported to the
      // subsequent read/write calls, hence we do not update the ioflag state for those.
      //
      // I/O on non-blocking sockets and blocking sockets with preceding poll, rely on this flag
      // state to return errors.
      // I/O on blocking socket without a preceding poll is one of the two below:
      // (1) If the peer resets the connection while the socket is blocked, return error.
      //     The caller of this routine can interpret POLLHUP to return appropriate error.
      // (2) If the read/write is called post connection reset, that is treated as I/O
      //     on a peer-closed socket handle.
      if (zx_signals & (ZXSIO_SIGNAL_CONNECTION_REFUSED | ZXSIO_SIGNAL_CONNECTION_RESET)) {
        ioflag() |= IOFLAG_SOCKET_HAS_ERROR;
      }
      events |= POLLIN | POLLOUT | POLLERR | POLLHUP | POLLRDHUP;
    }
    if (signals & ZXIO_SIGNAL_WRITE_DISABLED) {
      events |= POLLHUP | POLLOUT;
    }
    if (signals & ZXIO_SIGNAL_READ_DISABLED) {
      events |= POLLRDHUP | POLLIN;
    }
    *out_events = events;
  }

  Errno posix_ioctl(int req, va_list va) override {
    return zxsio_posix_ioctl(req, va, [this](int req, va_list va) {
      return posix_ioctl_inner(zxio_stream_socket().pipe.socket, req, va);
    });
  }

  zx_status_t bind(const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) override {
    return BaseSocket(zxio_stream_socket().client).bind(addr, addrlen, out_code);
  }

  zx_status_t connect(const struct sockaddr* addr, socklen_t addrlen, int16_t* out_code) override {
    return BaseSocket(zxio_stream_socket().client).connect(addr, addrlen, out_code);
  }

  zx_status_t listen(int backlog, int16_t* out_code) override {
    auto response = zxio_stream_socket().client.Listen(safemath::saturated_cast<int16_t>(backlog));
    zx_status_t status = response.status();
    if (status != ZX_OK) {
      return status;
    }
    auto const& result = response.Unwrap()->result;
    if (result.is_err()) {
      *out_code = static_cast<int16_t>(result.err());
      return ZX_OK;
    }
    ioflag() |= IOFLAG_SOCKET_LISTENING;
    *out_code = 0;
    return ZX_OK;
  }

  zx_status_t accept(int flags, struct sockaddr* addr, socklen_t* addrlen, zx_handle_t* out_handle,
                     int16_t* out_code) override {
    bool want_addr = addr != nullptr && addrlen != nullptr;
    auto response = zxio_stream_socket().client.Accept(want_addr);
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
    *out_handle = result.mutable_response().s.channel().release();
    auto const& out = result.response().addr;
    // Result address has invalid tag when it's not provided by the server (when want_addr
    // is false).
    // TODO(fxbug.dev/58503): Use better representation of nullable union when available.
    if (want_addr && !out.has_invalid_tag()) {
      *addrlen = static_cast<socklen_t>(fidl_to_sockaddr(out, addr, *addrlen));
    }
    return ZX_OK;
  }

  zx_status_t getsockname(struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) override {
    return BaseSocket(zxio_stream_socket().client).getsockname(addr, addrlen, out_code);
  }

  zx_status_t getpeername(struct sockaddr* addr, socklen_t* addrlen, int16_t* out_code) override {
    return BaseSocket(zxio_stream_socket().client).getpeername(addr, addrlen, out_code);
  }

  zx_status_t getsockopt(int level, int optname, void* optval, socklen_t* optlen,
                         int16_t* out_code) override {
    return BaseSocket(zxio_stream_socket().client)
        .getsockopt(level, optname, optval, optlen, out_code);
  }

  zx_status_t setsockopt(int level, int optname, const void* optval, socklen_t optlen,
                         int16_t* out_code) override {
    return BaseSocket(zxio_stream_socket().client)
        .setsockopt(level, optname, optval, optlen, out_code);
  }

  zx_status_t recvmsg(struct msghdr* msg, int flags, size_t* out_actual,
                      int16_t* out_code) override {
    *out_code = 0;

    auto status = flag_status(IO::RECV);
    if (status != ZX_OK) {
      return status;
    }

    status = recvmsg_inner(msg, flags, out_actual);
    if (status == ZX_ERR_INVALID_ARGS) {
      status = ZX_OK;
      *out_code = EFAULT;
    }
    return status;
  }

  zx_status_t sendmsg(const struct msghdr* msg, int flags, size_t* out_actual,
                      int16_t* out_code) override {
    *out_code = 0;

    auto status = flag_status(IO::SEND);
    if (status != ZX_OK) {
      return status;
    }

    // TODO(https://fxbug.dev/21106): support flags and control messages
    status = sendmsg_inner(msg, flags, out_actual);
    if (status == ZX_ERR_INVALID_ARGS) {
      status = ZX_OK;
      *out_code = EFAULT;
    }
    return status;
  }

  zx_status_t shutdown(int how, int16_t* out_code) override {
    *out_code = 0;
    zx_signals_t observed;
    zx_status_t status = zxio_stream_socket().pipe.socket.wait_one(
        ZX_SOCKET_PEER_CLOSED, zx::time::infinite_past(), &observed);
    if (status == ZX_OK || status == ZX_ERR_TIMED_OUT) {
      if (observed & ZX_SOCKET_PEER_CLOSED) {
        return ZX_ERR_NOT_CONNECTED;
      }
      return shutdown_inner(zxio_stream_socket().pipe.socket, how);
    }
    return status;
  }

 private:
  zxio_stream_socket_t& zxio_stream_socket() {
    return *reinterpret_cast<zxio_stream_socket_t*>(&zxio_storage().io);
  }

  enum class IO {
    SEND,
    RECV,
  };

  // Read the current ioflag state and try to infer the return zx_status.
  // Returns the appropriate ZX_ERR status if possible, else returns ZX_OK.
  zx_status_t flag_status(IO op) {
    if (ioflag() & IOFLAG_SOCKET_HAS_ERROR) {
      // Reset the socket connected or connecting flags, so that the subsequent calls do not return
      // the same error. Test:
      // src/connectivity/network/tests/bsdsocket_test.cc:TestListenWhileConnect
      if (ioflag() & IOFLAG_SOCKET_CONNECTED) {
        ioflag() ^= IOFLAG_SOCKET_CONNECTED;
        return ZX_ERR_CONNECTION_RESET;
      }
      if (ioflag() & IOFLAG_SOCKET_CONNECTING) {
        ioflag() ^= IOFLAG_SOCKET_CONNECTING;
        return ZX_ERR_CONNECTION_REFUSED;
      }
      return ZX_OK;
    }

    if (ioflag() & IOFLAG_SOCKET_CONNECTED) {
      return ZX_OK;
    }

    if (ioflag() & IOFLAG_SOCKET_CONNECTING) {
      return ZX_ERR_SHOULD_WAIT;
    }

    switch (op) {
      case IO::SEND:
        return ZX_ERR_BAD_STATE;
      case IO::RECV:
        return ZX_ERR_NOT_CONNECTED;
    }
  }

 protected:
  friend class fbl::internal::MakeRefCountedHelper<stream_socket>;
  friend class fbl::RefPtr<stream_socket>;

  stream_socket() = default;
  ~stream_socket() override = default;
};

}  // namespace fdio_internal

static constexpr zxio_ops_t zxio_stream_socket_ops = []() {
  zxio_ops_t ops = zxio_default_ops;
  ops.close = [](zxio_t* io) {
    auto zs = reinterpret_cast<zxio_stream_socket_t*>(io);
    zx_status_t channel_status = BaseSocket(zs->client).close();
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
    return BaseSocket(zs->client).clone(out_handle);
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

fdio_ptr fdio_stream_socket_create(zx::socket socket, fidl::ClientEnd<fsocket::StreamSocket> client,
                                   zx_info_socket_t info) {
  fdio_ptr io = fbl::MakeRefCounted<fdio_internal::stream_socket>();
  if (io == nullptr) {
    return nullptr;
  }
  zxio_storage_t& storage = io->zxio_storage();
  auto zs = new (&storage) zxio_stream_socket_t{
      .io = {},
      .pipe = {},
      .client = fidl::BindSyncClient(std::move(client)),
  };
  zxio_init(&zs->io, &zxio_stream_socket_ops);
  zxio_pipe_init(reinterpret_cast<zxio_storage_t*>(&zs->pipe), std::move(socket), info);
  return io;
}

bool fdio_is_socket(fdio_t* io) {
  if (!io) {
    return false;
  }
  const zxio_ops_t* ops = zxio_get_ops(&io->zxio_storage().io);
  return ops == &zxio_datagram_socket_ops || ops == &zxio_stream_socket_ops;
}

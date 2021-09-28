// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.net.name/cpp/wire.h>
#include <fidl/fuchsia.net/cpp/wire.h>
#include <fidl/fuchsia.posix.socket.raw/cpp/wire.h>
#include <ifaddrs.h>
#include <lib/fdio/io.h>
#include <lib/fit/defer.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <zircon/device/vfs.h>
#include <zircon/lookup.h>

#include <cerrno>
#include <cstdarg>
#include <cstring>
#include <mutex>

#include <fbl/auto_lock.h>

#include "fdio_unistd.h"
#include "internal.h"
#include "src/network/getifaddrs.h"

namespace fio = fuchsia_io;
namespace fnet = fuchsia_net;
namespace fnet_name = fuchsia_net_name;
namespace fsocket = fuchsia_posix_socket;
namespace frawsocket = fuchsia_posix_socket_raw;

__EXPORT
int socket(int domain, int type, int protocol) {
  fsocket::wire::Domain sock_domain;
  switch (domain) {
    case AF_INET:
      sock_domain = fsocket::wire::Domain::kIpv4;
      break;
    case AF_INET6:
      sock_domain = fsocket::wire::Domain::kIpv6;
      break;
    case AF_PACKET:
      return ERRNO(EPERM);
    default:
      return ERRNO(EPROTONOSUPPORT);
  }
  constexpr int kSockTypesMask = ~(SOCK_CLOEXEC | SOCK_NONBLOCK);
  fidl::ClientEnd<fio::Node> client_end;
  switch (type & kSockTypesMask) {
    case SOCK_STREAM:
      switch (protocol) {
        case IPPROTO_IP:
        case IPPROTO_TCP: {
          auto& provider = get_client<fsocket::Provider>();
          if (provider.is_error()) {
            return ERRNO(EIO);
          }

          auto result =
              provider->StreamSocket(sock_domain, fsocket::wire::StreamSocketProtocol::kTcp);
          if (result.status() != ZX_OK) {
            return ERROR(result.status());
          }
          if (result->result.is_err()) {
            return ERRNO(static_cast<int32_t>(result->result.err()));
          }
          client_end.channel() = result->result.mutable_response().s.TakeChannel();
        } break;
        default:
          return ERRNO(EPROTONOSUPPORT);
      }
      break;
    case SOCK_DGRAM: {
      fsocket::wire::DatagramSocketProtocol proto;
      switch (protocol) {
        case IPPROTO_IP:
        case IPPROTO_UDP:
          proto = fsocket::wire::DatagramSocketProtocol::kUdp;
          break;
        case IPPROTO_ICMP:
          if (sock_domain != fsocket::wire::Domain::kIpv4) {
            return ERRNO(EPROTONOSUPPORT);
          }
          proto = fsocket::wire::DatagramSocketProtocol::kIcmpEcho;
          break;
        case IPPROTO_ICMPV6:
          if (sock_domain != fsocket::wire::Domain::kIpv6) {
            return ERRNO(EPROTONOSUPPORT);
          }
          proto = fsocket::wire::DatagramSocketProtocol::kIcmpEcho;
          break;
        default:
          return ERRNO(EPROTONOSUPPORT);
      }

      auto& provider = get_client<fsocket::Provider>();
      if (provider.is_error()) {
        return ERRNO(EIO);
      }

      auto result = provider->DatagramSocket(sock_domain, proto);
      if (result.status() != ZX_OK) {
        return ERROR(result.status());
      }
      if (result->result.is_err()) {
        return ERRNO(static_cast<int32_t>(result->result.err()));
      }
      client_end.channel() = result->result.mutable_response().s.TakeChannel();
    } break;
    case SOCK_RAW: {
      if (protocol == 0) {
        return ERRNO(EPROTONOSUPPORT);
      }

      auto& provider = get_client<frawsocket::Provider>();
      if (provider.is_error()) {
        return ERRNO(EIO);
      }

      if ((protocol > std::numeric_limits<uint8_t>::max()) ||
          (protocol < std::numeric_limits<uint8_t>::min())) {
        return ERRNO(EINVAL);
      }
      frawsocket::wire::ProtocolAssociation proto_assoc;
      frawsocket::wire::Empty empty;
      uint8_t sock_protocol = static_cast<uint8_t>(protocol);
      // Sockets created with IPPROTO_RAW are only used to send packets as per
      // https://linux.die.net/man/7/raw,
      //
      //   A protocol of IPPROTO_RAW implies enabled IP_HDRINCL and is able to
      //   send any IP protocol that is specified in the passed header. Receiving
      //   of all IP protocols via IPPROTO_RAW is not possible using raw sockets.
      if (protocol == IPPROTO_RAW) {
        proto_assoc.set_unassociated(empty);
      } else {
        proto_assoc.set_associated(sock_protocol);
      }
      auto result = provider->Socket(sock_domain, proto_assoc);
      auto status = result.status();
      if (status != ZX_OK) {
        if (status == ZX_ERR_PEER_CLOSED) {
          // Client does not have access to the raw socket protocol.
          return ERRNO(EPERM);
        }
        return ERROR(status);
      }
      if (result->result.is_err()) {
        return ERRNO(static_cast<int32_t>(result->result.err()));
      }
      client_end.channel() = result->result.mutable_response().s.TakeChannel();
    } break;
    default:
      return ERRNO(EPROTONOSUPPORT);
  }

  zx::status io = fdio::create_with_describe(std::move(client_end));
  if (io.is_error()) {
    return ERROR(io.status_value());
  }

  if (type & SOCK_NONBLOCK) {
    io->ioflag() |= IOFLAG_NONBLOCK;
  }

  // TODO(fxbug.dev/30920): Implement CLOEXEC.
  // if (type & SOCK_CLOEXEC) {
  // }

  std::optional fd = bind_to_fd(io.value());
  if (fd.has_value()) {
    return fd.value();
  }
  return ERRNO(EMFILE);
}

__EXPORT
int connect(int fd, const struct sockaddr* addr, socklen_t len) {
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }

  int16_t out_code;
  zx_status_t status;
  if ((status = io->connect(addr, len, &out_code)) != ZX_OK) {
    return ERROR(status);
  }
  if (out_code == EINPROGRESS) {
    auto& ioflag = io->ioflag();
    if (!(ioflag & IOFLAG_NONBLOCK)) {
      if ((status = fdio_wait(io, FDIO_EVT_WRITABLE, zx::time::infinite(), nullptr)) != ZX_OK) {
        return ERROR(status);
      }
      // Call Connect() again after blocking to find connect's result.
      if ((status = io->connect(addr, len, &out_code)) != ZX_OK) {
        return ERROR(status);
      }
    }
  }

  if (out_code) {
    return ERRNO(out_code);
  }
  return 0;
}

template <typename F>
static int delegate(int fd, F fn) {
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  int16_t out_code;
  zx_status_t status = fn(io, &out_code);
  if (status != ZX_OK) {
    return ERROR(status);
  }
  if (out_code) {
    return ERRNO(out_code);
  }
  return out_code;
}

__EXPORT
int bind(int fd, const struct sockaddr* addr, socklen_t len) {
  return delegate(
      fd, [&](const fdio_ptr& io, int16_t* out_code) { return io->bind(addr, len, out_code); });
}

__EXPORT
int listen(int fd, int backlog) {
  return delegate(
      fd, [&](const fdio_ptr& io, int16_t* out_code) { return io->listen(backlog, out_code); });
}

__EXPORT
int accept4(int fd, struct sockaddr* __restrict addr, socklen_t* __restrict addrlen, int flags) {
  if (flags & ~SOCK_NONBLOCK) {
    return ERRNO(EINVAL);
  }
  if ((addr == nullptr) != (addrlen == nullptr)) {
    return ERRNO(EINVAL);
  }

  std::optional reservation = []() -> std::optional<std::pair<int, void (fdio_slot::*)()>> {
    fbl::AutoLock lock(&fdio_lock);
    for (int i = 0; i < FDIO_MAX_FD; ++i) {
      std::optional cleanup = fdio_fdtab[i].try_reserve();
      if (cleanup.has_value()) {
        return std::make_pair(i, cleanup.value());
      }
    }
    return std::nullopt;
  }();
  if (!reservation.has_value()) {
    return ERRNO(EMFILE);
  }
  auto [nfd, cleanup_getter] = reservation.value();
  // Lambdas are not allowed to reference local bindings.
  auto release = fit::defer([nfd = nfd, cleanup_getter = cleanup_getter]() {
    fbl::AutoLock lock(&fdio_lock);
    (fdio_fdtab[nfd].*cleanup_getter)();
  });

  zx::handle accepted;
  {
    zx_status_t status;
    int16_t out_code;

    fdio_ptr io = fd_to_io(fd);
    if (io == nullptr) {
      return ERRNO(EBADF);
    }

    const bool blocking = (io->ioflag() & IOFLAG_NONBLOCK) == 0;

    for (;;) {
      // We're going to manage blocking on the client side, so always ask the
      // provider for a non-blocking socket.
      if ((status = io->accept(flags | SOCK_NONBLOCK, addr, addrlen,
                               accepted.reset_and_get_address(), &out_code)) != ZX_OK) {
        break;
      }

      // This condition should also apply to EAGAIN; it happens to have the
      // same value as EWOULDBLOCK.
      if (out_code == EWOULDBLOCK) {
        if (blocking) {
          if ((status = fdio_wait(io, FDIO_EVT_READABLE, zx::time::infinite(), nullptr)) != ZX_OK) {
            break;
          }
          continue;
        }
      }
      break;
    }

    if (status != ZX_OK) {
      return ERROR(status);
    }
    if (out_code) {
      return ERRNO(out_code);
    }
  }

  zx::status accepted_io =
      fdio::create_with_describe(fidl::ClientEnd<fio::Node>(zx::channel(std::move(accepted))));
  if (accepted_io.is_error()) {
    return ERROR(accepted_io.status_value());
  }

  if (flags & SOCK_NONBLOCK) {
    accepted_io->ioflag() |= IOFLAG_NONBLOCK;
  }

  fbl::AutoLock lock(&fdio_lock);
  if (fdio_fdtab[nfd].try_fill(accepted_io.value())) {
    return nfd;
  }

  // Someone stomped our reservation. Try to find a new slot.
  //
  // Note that this reservation business is subtle but sound; consider the following scenario:
  // - T1: a reservation is made, |accept| is called but doesn't return (remote is slow)
  // - T2: |dup2| is called and evicts the reservation
  // - T2: |close| is called and closes the file descriptor created by |dup2|
  // - T2: a reservation is made, |accept| is called but doesn't return (remote is slow)
  // - T1: |accept| returns and fulfills the reservation which no longer belongs to it
  // - T2: |accept| returns and discovers its reservation is gone, and looks for a new slot
  //
  // Ownership of reservations isn't maintained, but that should be OK as long as it isn't assumed.
  for (int i = 0; i < FDIO_MAX_FD; ++i) {
    if (fdio_fdtab[nfd].try_set(accepted_io.value())) {
      return i;
    }
  }
  return ERRNO(EMFILE);
}

__EXPORT
int _getaddrinfo_from_dns(struct address buf[MAXADDRS], char canon[256], const char* name,
                          int family) {
  auto& name_lookup = get_client<fnet_name::Lookup>();
  if (name_lookup.is_error()) {
    errno = fdio_status_to_errno(name_lookup.status_value());
    return EAI_SYSTEM;
  }

  fnet_name::wire::LookupIpOptions::Frame_ frame;
  fnet_name::wire::LookupIpOptions options(
      fidl::ObjectView<fnet_name::wire::LookupIpOptions::Frame_>::FromExternal(&frame));
  // TODO(https://fxbug.dev/76522): Use address sorting from the DNS service.
  switch (family) {
    case AF_UNSPEC:
      options.set_ipv4_lookup(true);
      options.set_ipv6_lookup(true);
      break;
    case AF_INET:
      options.set_ipv4_lookup(true);
      break;
    case AF_INET6:
      options.set_ipv6_lookup(true);
      break;
    default:
      return EAI_FAMILY;
  }

  // Explicitly allocating message buffers to avoid heap allocation.
  fidl::Buffer<fidl::WireRequest<fnet_name::Lookup::LookupIp>> request_buffer;
  fidl::Buffer<fidl::WireResponse<fnet_name::Lookup::LookupIp>> response_buffer;
  const fidl::WireUnownedResult fidl_result = name_lookup->LookupIp(
      request_buffer.view(), fidl::StringView::FromExternal(name), options, response_buffer.view());
  if (!fidl_result.ok()) {
    errno = fdio_status_to_errno(fidl_result.status());
    return EAI_SYSTEM;
  }
  const fnet_name::wire::LookupLookupIpResult& wire_result = fidl_result.value().result;
  switch (wire_result.which()) {
    case fnet_name::wire::LookupLookupIpResult::Tag::kResponse: {
      int count = 0;
      const fnet_name::wire::LookupResult& result = wire_result.response().result;
      if (result.has_addresses()) {
        for (const fnet::wire::IpAddress& addr : result.addresses()) {
          switch (addr.which()) {
            case fnet::wire::IpAddress::Tag::kIpv4: {
              buf[count].family = AF_INET;
              buf[count].scopeid = 0;
              const auto& octets = addr.ipv4().addr;
              std::copy(octets.begin(), octets.end(), buf[count].addr);
              buf[count].sortkey = 0;
              count++;
            } break;
            case fnet::wire::IpAddress::Tag::kIpv6: {
              buf[count].family = AF_INET6;
              // TODO(https://fxbug.dev/21415): Figure out a way to expose scope ID for IPv6
              // addresses.
              buf[count].scopeid = 0;
              const auto& octets = addr.ipv6().addr;
              std::copy(octets.begin(), octets.end(), buf[count].addr);
              buf[count].sortkey = 0;
              count++;
            } break;
          }
        }
      }

      return count;
    }
    case fnet_name::wire::LookupLookupIpResult::Tag::kErr:
      switch (wire_result.err()) {
        case fnet_name::wire::LookupError::kNotFound:
          return EAI_NONAME;
        case fnet_name::wire::LookupError::kTransient:
          return EAI_AGAIN;
        case fnet_name::wire::LookupError::kInvalidArgs:
          return EAI_FAIL;
        case fnet_name::wire::LookupError::kInternalError:
          errno = EIO;
          return EAI_SYSTEM;
      }
  }
}
__EXPORT
int getsockname(int fd, struct sockaddr* __restrict addr, socklen_t* __restrict len) {
  if (len == nullptr || addr == nullptr) {
    return ERRNO(EINVAL);
  }

  return delegate(fd, [&](const fdio_ptr& io, int16_t* out_code) {
    return io->getsockname(addr, len, out_code);
  });
}

__EXPORT
int getpeername(int fd, struct sockaddr* __restrict addr, socklen_t* __restrict len) {
  if (len == nullptr || addr == nullptr) {
    return ERRNO(EINVAL);
  }

  return delegate(fd, [&](const fdio_ptr& io, int16_t* out_code) {
    return io->getpeername(addr, len, out_code);
  });
}

__EXPORT
int getsockopt(int fd, int level, int optname, void* __restrict optval,
               socklen_t* __restrict optlen) {
  if (optval == nullptr || optlen == nullptr) {
    return ERRNO(EFAULT);
  }

  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }

  // Handle client-maintained socket options.
  if (level == SOL_SOCKET) {
    auto do_timeout = [&](zx::duration& timeout) {
      if (*optlen < sizeof(struct timeval)) {
        return ERRNO(EINVAL);
      }
      *optlen = sizeof(struct timeval);
      auto duration_tv = static_cast<struct timeval*>(optval);
      if (timeout == zx::duration::infinite()) {
        duration_tv->tv_sec = 0;
        duration_tv->tv_usec = 0;
      } else {
        duration_tv->tv_sec = timeout.to_secs();
        duration_tv->tv_usec = (timeout - zx::sec(duration_tv->tv_sec)).to_usecs();
      }
      return 0;
    };
    switch (optname) {
      case SO_RCVTIMEO:
        return do_timeout(io->rcvtimeo());
      case SO_SNDTIMEO:
        return do_timeout(io->sndtimeo());
    }
  }

  int16_t out_code;
  zx_status_t status = io->getsockopt(level, optname, optval, optlen, &out_code);
  if (status != ZX_OK) {
    return ERROR(status);
  }
  if (out_code) {
    return ERRNO(out_code);
  }
  return 0;
}

__EXPORT
int setsockopt(int fd, int level, int optname, const void* optval, socklen_t optlen) {
  fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }

  // Handle client-maintained socket options.
  switch (level) {
    case SOL_SOCKET: {
      auto do_timeout = [&](zx::duration& timeout) {
        if (optlen < sizeof(struct timeval)) {
          return ERRNO(EINVAL);
        }
        const struct timeval* duration_tv = static_cast<const struct timeval*>(optval);
        // https://github.com/torvalds/linux/blob/bd2463ac7d7ec51d432f23bf0e893fb371a908cd/net/core/sock.c#L392-L393
        constexpr int kUsecPerSec = 1000000;
        if (duration_tv->tv_usec < 0 || duration_tv->tv_usec >= kUsecPerSec) {
          return ERRNO(EDOM);
        }
        if (duration_tv->tv_sec || duration_tv->tv_usec) {
          timeout = zx::sec(duration_tv->tv_sec) + zx::usec(duration_tv->tv_usec);
        } else {
          timeout = zx::duration::infinite();
        }
        return 0;
      };
      switch (optname) {
        case SO_RCVTIMEO:
          return do_timeout(io->rcvtimeo());
        case SO_SNDTIMEO:
          return do_timeout(io->sndtimeo());
      }
      break;
    }
    case IPPROTO_IP:
      // For each option, Linux handles the optval checks differently.
      // ref: net/ipv4/ip_sockglue.c, net/ipv6/ipv6_sockglue.c
      switch (optname) {
        case IP_TOS:
          if (optval == nullptr) {
            return ERRNO(EFAULT);
          }
          break;
        default:
          break;
      }
      break;
    case IPPROTO_IPV6:
      switch (optname) {
        case IPV6_TCLASS:
          if (optval == nullptr) {
            return 0;
          }
          break;
        default:
          break;
      }
      break;
    default:
      break;
  }

  int16_t out_code;
  zx_status_t status = io->setsockopt(level, optname, optval, optlen, &out_code);
  if (status != ZX_OK) {
    return ERROR(status);
  }
  if (out_code) {
    return ERRNO(out_code);
  }
  return 0;
}

// TODO(https://fxbug.dev/30719): set ifa_ifu.ifu_broadaddr and ifa_ifu.ifu_dstaddr.
//
// AF_PACKET addresses containing lower-level details about the interfaces are not included in the
// result list because raw sockets are not supported on Fuchsia.
__EXPORT
int getifaddrs(struct ifaddrs** ifap) {
  auto& provider = get_client<fsocket::Provider>();
  if (provider.is_error()) {
    return ERRNO(provider.error_value());
  }

  auto response = provider->GetInterfaceAddresses();
  zx_status_t status = response.status();
  if (status != ZX_OK) {
    return ERROR(status);
  }

  for (const auto& iface : response.Unwrap()->interfaces) {
    if (!iface.has_name() || !iface.has_addresses()) {
      continue;
    }

    const auto& if_name = iface.name();
    for (const auto& address : iface.addresses()) {
      auto ifs = static_cast<struct ifaddrs_storage*>(calloc(1, sizeof(struct ifaddrs_storage)));
      if (ifs == nullptr) {
        return -1;
      }
      const size_t n = std::min(if_name.size(), sizeof(ifs->name));
      memcpy(ifs->name, if_name.data(), n);
      ifs->name[n] = 0;
      ifs->ifa.ifa_name = ifs->name;

      const auto& addr = address.addr;
      const uint8_t prefix_len = address.prefix_len;

      switch (addr.which()) {
        case fnet::wire::IpAddress::Tag::kIpv4: {
          const auto& addr_bytes = addr.ipv4().addr;
          copy_addr(&ifs->ifa.ifa_addr, AF_INET, &ifs->addr,
                    const_cast<uint8_t*>(addr_bytes.data()), addr_bytes.size(),
                    static_cast<uint32_t>(iface.id()));
          gen_netmask(&ifs->ifa.ifa_netmask, AF_INET, &ifs->netmask, prefix_len);
          break;
        }
        case fnet::wire::IpAddress::Tag::kIpv6: {
          const auto& addr_bytes = addr.ipv6().addr;
          copy_addr(&ifs->ifa.ifa_addr, AF_INET6, &ifs->addr,
                    const_cast<uint8_t*>(addr_bytes.data()), addr_bytes.size(),
                    static_cast<uint32_t>(iface.id()));
          gen_netmask(&ifs->ifa.ifa_netmask, AF_INET6, &ifs->netmask, prefix_len);
          break;
        }
      }

      if (iface.has_flags()) {
        ifs->ifa.ifa_flags = static_cast<uint16_t>(iface.interface_flags());
      }

      *ifap = &ifs->ifa;
      ifap = &ifs->ifa.ifa_next;
    }
  }

  return 0;
}

__EXPORT
void freeifaddrs(struct ifaddrs* ifp) {
  struct ifaddrs* n;
  while (ifp) {
    n = ifp->ifa_next;
    free(ifp);
    ifp = n;
  }
}

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/net/llcpp/fidl.h>
#include <ifaddrs.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
#include <net/if.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <zircon/device/vfs.h>
#include <zircon/lookup.h>

#include <cerrno>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include <fbl/auto_call.h>

#include "private.h"
#include "src/network/getifaddrs.h"
#include "unistd.h"

namespace fio = ::llcpp::fuchsia::io;
namespace fnet = ::llcpp::fuchsia::net;
namespace fsocket = ::llcpp::fuchsia::posix::socket;

#define MAKE_GET_SERVICE(fn_name, symbol)                        \
  zx_status_t fn_name(symbol::SyncClient** out) {                \
    static symbol::SyncClient* saved;                            \
    static std::once_flag once;                                  \
    static zx_status_t status;                                   \
    std::call_once(once, [&]() {                                 \
      zx::channel out;                                           \
      status = fdio_service_connect_by_name(symbol::Name, &out); \
      if (status != ZX_OK) {                                     \
        return;                                                  \
      }                                                          \
      static symbol::SyncClient client(std::move(out));          \
      saved = &client;                                           \
    });                                                          \
    if (status != ZX_OK) {                                       \
      return status;                                             \
    }                                                            \
    *out = saved;                                                \
    return ZX_OK;                                                \
  }

namespace {
MAKE_GET_SERVICE(get_name_lookup, fnet::NameLookup)
}
MAKE_GET_SERVICE(fdio_get_socket_provider, fsocket::Provider)

__EXPORT
int socket(int domain, int type, int protocol) {
  fsocket::Provider::SyncClient* provider;
  zx_status_t status = fdio_get_socket_provider(&provider);
  if (status != ZX_OK) {
    return ERRNO(EIO);
  }

  fsocket::Domain sock_domain;
  switch (domain) {
    case AF_INET:
      sock_domain = fsocket::Domain::IPV4;
      break;
    case AF_INET6:
      sock_domain = fsocket::Domain::IPV6;
      break;
    case AF_PACKET:
      return ERRNO(EPERM);
    default:
      return ERRNO(EPROTONOSUPPORT);
  }
  constexpr int kSockTypesMask = ~(SOCK_CLOEXEC | SOCK_NONBLOCK);
  zx::channel socket_channel;
  switch (type & kSockTypesMask) {
    case SOCK_STREAM:
      switch (protocol) {
        case IPPROTO_IP:
        case IPPROTO_TCP: {
          auto result = provider->StreamSocket(sock_domain, fsocket::StreamSocketProtocol::TCP);
          if (result.status() != ZX_OK) {
            return ERROR(result.status());
          }
          if (result->result.is_err()) {
            return ERRNO(static_cast<int32_t>(result->result.err()));
          }
          socket_channel = std::move(result->result.mutable_response().s);
        } break;
        default:
          return ERRNO(EPROTONOSUPPORT);
      }
      break;
    case SOCK_DGRAM: {
      fsocket::DatagramSocketProtocol proto;
      switch (protocol) {
        case IPPROTO_IP:
        case IPPROTO_UDP:
          proto = fsocket::DatagramSocketProtocol::UDP;
          break;
        case IPPROTO_ICMP:
          if (sock_domain != fsocket::Domain::IPV4) {
            return ERRNO(EPROTONOSUPPORT);
          }
          proto = fsocket::DatagramSocketProtocol::ICMP_ECHO;
          break;
        case IPPROTO_ICMPV6:
          if (sock_domain != fsocket::Domain::IPV6) {
            return ERRNO(EPROTONOSUPPORT);
          }
          proto = fsocket::DatagramSocketProtocol::ICMP_ECHO;
          break;
        default:
          return ERRNO(EPROTONOSUPPORT);
      }
      auto result = provider->DatagramSocket(sock_domain, proto);
      if (result.status() != ZX_OK) {
        return ERROR(result.status());
      }
      if (result->result.is_err()) {
        return ERRNO(static_cast<int32_t>(result->result.err()));
      }
      socket_channel = std::move(result->result.mutable_response().s);
    } break;
    default:
      return ERRNO(EPROTONOSUPPORT);
  }

  fdio_t* io;
  status = fdio_from_channel(std::move(socket_channel), &io);
  if (status != ZX_OK) {
    return ERROR(status);
  }

  // TODO(tamird): we're not handling this flag in fdio_from_channel, which seems bad.
  if (type & SOCK_NONBLOCK) {
    *fdio_get_ioflag(io) |= IOFLAG_NONBLOCK;
  }

  // TODO(fxbug.dev/30920): Implement CLOEXEC.
  // if (type & SOCK_CLOEXEC) {
  // }

  int fd = fdio_bind_to_fd(io, -1, 0);
  if (fd < 0) {
    fdio_release(io);
    return ERRNO(EMFILE);
  }
  return fd;
}

__EXPORT
int connect(int fd, const struct sockaddr* addr, socklen_t len) {
  fdio_t* io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }

  int16_t out_code;
  zx_status_t status;
  if ((status = fdio_get_ops(io)->connect(io, addr, len, &out_code)) != ZX_OK) {
    fdio_release(io);
    return ERROR(status);
  }
  if (out_code == EINPROGRESS) {
    bool nonblocking = *fdio_get_ioflag(io) & IOFLAG_NONBLOCK;

    if (nonblocking) {
      *fdio_get_ioflag(io) |= IOFLAG_SOCKET_CONNECTING;
    } else {
      if ((status = fdio_wait(io, FDIO_EVT_WRITABLE, zx::time::infinite(), nullptr)) != ZX_OK) {
        fdio_release(io);
        return ERROR(status);
      }
      // Call Connect() again after blocking to find connect's result.
      if ((status = fdio_get_ops(io)->connect(io, addr, len, &out_code)) != ZX_OK) {
        fdio_release(io);
        return ERROR(status);
      }
    }
  }

  switch (out_code) {
    case 0: {
      *fdio_get_ioflag(io) |= IOFLAG_SOCKET_CONNECTED;
      fdio_release(io);
      return out_code;
    }

    default: {
      fdio_release(io);
      return ERRNO(out_code);
    }
  }
}

template <typename F>
static int delegate(int fd, F fn) {
  fdio_t* io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  int16_t out_code;
  zx_status_t status = fn(io, &out_code);
  fdio_release(io);
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
  return delegate(fd, [&](fdio_t* io, int16_t* out_code) {
    return fdio_get_ops(io)->bind(io, addr, len, out_code);
  });
}

__EXPORT
int listen(int fd, int backlog) {
  return delegate(fd, [&](fdio_t* io, int16_t* out_code) {
    return fdio_get_ops(io)->listen(io, backlog, out_code);
  });
}

__EXPORT
int accept4(int fd, struct sockaddr* __restrict addr, socklen_t* __restrict addrlen, int flags) {
  if (flags & ~SOCK_NONBLOCK) {
    return ERRNO(EINVAL);
  }
  if ((addr == nullptr) != (addrlen == nullptr)) {
    return ERRNO(EINVAL);
  }

  int nfd = fdio_reserve_fd(0);
  if (nfd < 0) {
    return nfd;
  }

  zx::handle accepted;
  {
    zx_status_t status;
    int16_t out_code;

    fdio_t* io = fd_to_io(fd);
    if (io == nullptr) {
      fdio_release_reserved(nfd);
      return ERRNO(EBADF);
    }

    bool nonblocking = *fdio_get_ioflag(io) & IOFLAG_NONBLOCK;

    for (;;) {
      // We're going to manage blocking on the client side, so always ask the
      // provider for a non-blocking socket.
      if ((status = fdio_get_ops(io)->accept(io, flags | SOCK_NONBLOCK, addr, addrlen,
                                             accepted.reset_and_get_address(), &out_code)) !=
          ZX_OK) {
        break;
      }

      // This condition should also apply to EAGAIN; it happens to have the
      // same value as EWOULDBLOCK.
      if (out_code == EWOULDBLOCK) {
        if (!nonblocking) {
          if ((status = fdio_wait(io, FDIO_EVT_READABLE, zx::time::infinite(), nullptr)) != ZX_OK) {
            break;
          }
          continue;
        }
      }
      break;
    }
    fdio_release(io);

    if (status != ZX_OK) {
      fdio_release_reserved(nfd);
      return ERROR(status);
    }
    if (out_code) {
      fdio_release_reserved(nfd);
      return ERRNO(out_code);
    }
  }

  fdio_t* accepted_io;
  zx_status_t status = fdio_create(accepted.release(), &accepted_io);
  if (status != ZX_OK) {
    fdio_release_reserved(nfd);
    return ERROR(status);
  }

  // TODO(tamird): we're not handling this flag in fdio_from_channel, which seems bad.
  if (flags & SOCK_NONBLOCK) {
    *fdio_get_ioflag(accepted_io) |= IOFLAG_NONBLOCK;
  }

  nfd = fdio_assign_reserved(nfd, accepted_io);
  if (nfd < 0) {
    fdio_release(accepted_io);
  }
  return nfd;
}

__EXPORT
int _getaddrinfo_from_dns(struct address buf[MAXADDRS], char canon[256], const char* name,
                          int family) {
  fnet::NameLookup::SyncClient* name_lookup;
  zx_status_t status = get_name_lookup(&name_lookup);
  if (status != ZX_OK) {
    errno = fdio_status_to_errno(status);
    return EAI_SYSTEM;
  }

  fnet::LookupIpOptions options;
  switch (family) {
    case AF_UNSPEC:
      options = fnet::LookupIpOptions::V4_ADDRS | fnet::LookupIpOptions::V6_ADDRS;
      break;
    case AF_INET:
      options = fnet::LookupIpOptions::V4_ADDRS;
      break;
    case AF_INET6:
      options = fnet::LookupIpOptions::V6_ADDRS;
      break;
    default:
      return EAI_FAMILY;
  }

  // Explicitly allocating message buffers to avoid heap allocation.
  fidl::Buffer<fnet::NameLookup::LookupIpRequest> request_buffer;
  fidl::Buffer<fnet::NameLookup::LookupIpResponse> response_buffer;
  auto result = name_lookup->LookupIp(request_buffer.view(), fidl::unowned_str(name, strlen(name)),
                                      options, response_buffer.view());
  status = result.status();
  if (status != ZX_OK) {
    errno = fdio_status_to_errno(status);
    return EAI_SYSTEM;
  }
  fnet::NameLookup_LookupIp_Result& lookup_ip_result = result.Unwrap()->result;
  if (lookup_ip_result.is_err()) {
    switch (lookup_ip_result.err()) {
      case fnet::LookupError::NOT_FOUND:
        return EAI_NONAME;
      case fnet::LookupError::TRANSIENT:
        return EAI_AGAIN;
      case fnet::LookupError::INVALID_ARGS:
        return EAI_FAIL;
      case fnet::LookupError::INTERNAL_ERROR:  // fallthrough
      default:
        errno = EIO;
        return EAI_SYSTEM;
    }
  }

  const auto& response = lookup_ip_result.response().addr;
  int count = 0;

  if (options & fnet::LookupIpOptions::V4_ADDRS) {
    for (uint64_t i = 0; i < response.ipv4_addrs.count() && count < MAXADDRS; i++) {
      buf[count].family = AF_INET;
      buf[count].scopeid = 0;
      auto addr = response.ipv4_addrs.at(i).addr;
      memcpy(buf[count].addr, addr.data(), addr.size());
      buf[count].sortkey = 0;
      count++;
    }
  }
  if (options & fnet::LookupIpOptions::V6_ADDRS) {
    for (uint64_t i = 0; i < response.ipv6_addrs.count() && count < MAXADDRS; i++) {
      buf[count].family = AF_INET6;
      buf[count].scopeid = 0;  // TODO(fxbug.dev/21415): Figure out a way to expose scope ID
      auto addr = response.ipv6_addrs.at(i).addr;
      memcpy(buf[count].addr, addr.data(), addr.size());
      buf[count].sortkey = 0;
      count++;
    }
  }

  // TODO(fxbug.dev/21414) support CNAME

  return count;
}
__EXPORT
int getsockname(int fd, struct sockaddr* __restrict addr, socklen_t* __restrict len) {
  if (len == nullptr || addr == nullptr) {
    return ERRNO(EINVAL);
  }

  return delegate(fd, [&](fdio_t* io, int16_t* out_code) {
    return fdio_get_ops(io)->getsockname(io, addr, len, out_code);
  });
}

__EXPORT
int getpeername(int fd, struct sockaddr* __restrict addr, socklen_t* __restrict len) {
  if (len == nullptr || addr == nullptr) {
    return ERRNO(EINVAL);
  }

  return delegate(fd, [&](fdio_t* io, int16_t* out_code) {
    return fdio_get_ops(io)->getpeername(io, addr, len, out_code);
  });
}

__EXPORT
int getsockopt(int fd, int level, int optname, void* __restrict optval,
               socklen_t* __restrict optlen) {
  if (optval == nullptr || optlen == nullptr) {
    return ERRNO(EFAULT);
  }

  fdio_t* io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  auto clean_io = fbl::MakeAutoCall([io] { fdio_release(io); });

  // Handle client-maintained socket options.
  if (level == SOL_SOCKET) {
    const zx::duration* timeout = nullptr;
    switch (optname) {
      case SO_RCVTIMEO:
        timeout = fdio_get_rcvtimeo(io);
        break;
      case SO_SNDTIMEO:
        timeout = fdio_get_sndtimeo(io);
        break;
      default:
        break;
    }
    if (timeout) {
      if (optlen == nullptr || *optlen < sizeof(struct timeval)) {
        return ERRNO(EINVAL);
      }
      *optlen = sizeof(struct timeval);
      auto duration_tv = static_cast<struct timeval*>(optval);
      if (*timeout == zx::duration::infinite()) {
        duration_tv->tv_sec = 0;
        duration_tv->tv_usec = 0;
      } else {
        duration_tv->tv_sec = timeout->to_secs();
        duration_tv->tv_usec = (*timeout - zx::sec(duration_tv->tv_sec)).to_usecs();
      }
      return 0;
    }
  }

  int16_t out_code;
  zx_status_t status = fdio_get_ops(io)->getsockopt(io, level, optname, optval, optlen, &out_code);
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
  fdio_t* io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  auto clean_io = fbl::MakeAutoCall([io] { fdio_release(io); });

  switch (level) {
    case SOL_SOCKET: {
      // Handle client-maintained socket options.
      zx::duration* timeout = nullptr;
      switch (optname) {
        case SO_RCVTIMEO:
          timeout = fdio_get_rcvtimeo(io);
          break;
        case SO_SNDTIMEO:
          timeout = fdio_get_sndtimeo(io);
          break;
      }
      if (timeout) {
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
          *timeout = zx::sec(duration_tv->tv_sec) + zx::usec(duration_tv->tv_usec);
        } else {
          *timeout = zx::duration::infinite();
        }
        return 0;
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
  zx_status_t status = fdio_get_ops(io)->setsockopt(io, level, optname, optval, optlen, &out_code);
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
  fsocket::Provider::SyncClient* provider = nullptr;
  zx_status_t status = fdio_get_socket_provider(&provider);
  if (status != ZX_OK) {
    return ERROR(status);
  }

  auto response = provider->GetInterfaceAddresses();
  status = response.status();
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
        case fnet::IpAddress::Tag::kIpv4: {
          const auto& addr_bytes = addr.ipv4().addr;
          copy_addr(&ifs->ifa.ifa_addr, AF_INET, &ifs->addr,
                    const_cast<uint8_t*>(addr_bytes.data()), addr_bytes.size(), iface.id());
          gen_netmask(&ifs->ifa.ifa_netmask, AF_INET, &ifs->netmask, prefix_len);
          break;
        }
        case fnet::IpAddress::Tag::kIpv6: {
          const auto& addr_bytes = addr.ipv6().addr;
          copy_addr(&ifs->ifa.ifa_addr, AF_INET6, &ifs->addr,
                    const_cast<uint8_t*>(addr_bytes.data()), addr_bytes.size(), iface.id());
          gen_netmask(&ifs->ifa.ifa_netmask, AF_INET6, &ifs->netmask, prefix_len);
          break;
        }
      }

      if (iface.has_flags()) {
        ifs->ifa.ifa_flags = iface.flags();
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

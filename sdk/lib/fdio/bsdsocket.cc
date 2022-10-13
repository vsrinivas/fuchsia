// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fidl/fuchsia.net.name/cpp/wire.h>
#include <fidl/fuchsia.net/cpp/wire.h>
#include <fidl/fuchsia.posix.socket.packet/cpp/wire.h>
#include <fidl/fuchsia.posix.socket.raw/cpp/wire.h>
#include <lib/fdio/io.h>
#include <lib/fit/defer.h>
#include <lib/zxio/bsdsocket.h>
#include <lib/zxio/cpp/create_with_type.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <zircon/lookup.h>

#include <cerrno>
#include <cstdarg>
#include <mutex>

#include <fbl/auto_lock.h>
#include <fbl/ref_ptr.h>
#include <fbl/unique_fd.h>

#include "sdk/lib/fdio/fdio_unistd.h"
#include "sdk/lib/fdio/get_client.h"
#include "sdk/lib/fdio/internal.h"
#include "sdk/lib/fdio/socket.h"
#include "src/network/getifaddrs.h"

namespace fnet = fuchsia_net;
namespace fnet_name = fuchsia_net_name;
namespace fsocket = fuchsia_posix_socket;
namespace frawsocket = fuchsia_posix_socket_raw;
namespace fpacketsocket = fuchsia_posix_socket_packet;

constexpr int kSockTypesMask = ~(SOCK_CLOEXEC | SOCK_NONBLOCK);

template <typename T>
zx_status_t get_socket_provider(zx_handle_t* provider_handle) {
  const auto& provider = get_client<T>();
  if (provider.is_error()) {
    return provider.error_value();
  }
  *provider_handle = provider.value().client_end().channel().get();
  return ZX_OK;
}

__EXPORT
int socket(int domain, int type, int protocol) {
  zxio_service_connector service_connector = [](const char* service_name,
                                                zx_handle_t* provider_handle) {
    if (strcmp(service_name, fidl::DiscoverableProtocolName<fpacketsocket::Provider>) == 0) {
      return get_socket_provider<fpacketsocket::Provider>(provider_handle);
    }
    if (strcmp(service_name, fidl::DiscoverableProtocolName<frawsocket::Provider>) == 0) {
      return get_socket_provider<frawsocket::Provider>(provider_handle);
    }
    if (strcmp(service_name, fidl::DiscoverableProtocolName<fsocket::Provider>) == 0) {
      return get_socket_provider<fsocket::Provider>(provider_handle);
    }
    return ZX_ERR_INVALID_ARGS;
  };

  int16_t out_code;
  zx::status result = fdio::create([&](zxio_storage_alloc allocator, void** out_context) {
    return zxio_socket(service_connector, domain, type & kSockTypesMask, protocol, allocator,
                       out_context, &out_code);
  });
  if (result.is_error()) {
    const zx_status_t status = result.status_value();
    if (status == ZX_ERR_PEER_CLOSED) {
      // If we got a peer closed error, then it usually means that we
      // do not have the socket provider protocol in our sandbox which
      // means we do not have access. Note that this is a best guess.
      return ERRNO(EPERM);
    }
    return ERROR(status);
  }
  if (out_code) {
    return ERRNO(out_code);
  }
  const fdio_ptr io = result.value();

  if (type & SOCK_NONBLOCK) {
    io->ioflag() |= IOFLAG_NONBLOCK;
  }

  // TODO(https://fxbug.dev/30920): Implement CLOEXEC.
  // if (type & SOCK_CLOEXEC) {
  // }

  std::optional fd = bind_to_fd(io);
  if (fd.has_value()) {
    return fd.value();
  }
  return ERROR(ZX_ERR_NO_MEMORY);
}

__EXPORT
int connect(int fd, const struct sockaddr* addr, socklen_t len) {
  const fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }

  int16_t out_code;
  if (const zx_status_t status = zxio_connect(&io->zxio_storage().io, addr, len, &out_code);
      status != ZX_OK) {
    return ERROR(status);
  }
  if (out_code == EINPROGRESS) {
    auto& ioflag = io->ioflag();
    if (!(ioflag & IOFLAG_NONBLOCK)) {
      if (const zx_status_t status =
              fdio_wait(io, FDIO_EVT_WRITABLE, zx::time::infinite(), nullptr);
          status != ZX_OK) {
        return ERROR(status);
      }
      // Call Connect() again after blocking to find connect's result.
      if (const zx_status_t status = zxio_connect(&io->zxio_storage().io, addr, len, &out_code);
          status != ZX_OK) {
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
  const fdio_ptr io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }
  int16_t out_code;
  const zx_status_t status = fn(io, &out_code);
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
  return delegate(fd, [&](const fdio_ptr& io, int16_t* out_code) {
    return zxio_bind(&io->zxio_storage().io, addr, len, out_code);
  });
}

__EXPORT
int listen(int fd, int backlog) {
  return delegate(fd, [&](const fdio_ptr& io, int16_t* out_code) {
    return zxio_listen(&io->zxio_storage().io, backlog, out_code);
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

  std::optional reservation = []() -> std::optional<std::pair<int, void (fdio_slot::*)()>> {
    const fbl::AutoLock lock(&fdio_lock);
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
    const fbl::AutoLock lock(&fdio_lock);
    (fdio_fdtab[nfd].*cleanup_getter)();
  });

  const fdio_ptr accepted_io = fdio_socket_allocate();
  if (accepted_io == nullptr) {
    return ZX_ERR_NO_MEMORY;
  }
  {
    zx_status_t status;
    int16_t out_code;

    const fdio_ptr io = fd_to_io(fd);
    if (io == nullptr) {
      return ERRNO(EBADF);
    }

    const bool blocking = (io->ioflag() & IOFLAG_NONBLOCK) == 0;

    for (;;) {
      // We're going to manage blocking on the client side, so always ask the
      // provider for a non-blocking socket.
      status = zxio_accept(&io->zxio_storage().io, addr, addrlen, &accepted_io->zxio_storage(),
                           &out_code);
      if (status != ZX_OK) {
        break;
      }

      // This condition should also apply to EAGAIN; it happens to have the
      // same value as EWOULDBLOCK.
      if (out_code == EWOULDBLOCK) {
        if (blocking) {
          status = fdio_wait(io, FDIO_EVT_READABLE, zx::time::infinite(), nullptr);
          if (status != ZX_OK) {
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

  if (flags & SOCK_NONBLOCK) {
    accepted_io->ioflag() |= IOFLAG_NONBLOCK;
  }

  const fbl::AutoLock lock(&fdio_lock);
  if (fdio_fdtab[nfd].try_fill(accepted_io)) {
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
    if (fdio_fdtab[nfd].try_set(accepted_io)) {
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

  fidl::WireTableFrame<fnet_name::wire::LookupIpOptions> frame;
  fidl::WireTableExternalBuilder options = fnet_name::wire::LookupIpOptions::ExternalBuilder(
      fidl::ObjectView<fidl::WireTableFrame<fnet_name::wire::LookupIpOptions>>::FromExternal(
          &frame));
  // TODO(https://fxbug.dev/76522): Use address sorting from the DNS service.
  switch (family) {
    case AF_UNSPEC:
      options.ipv4_lookup(true);
      options.ipv6_lookup(true);
      break;
    case AF_INET:
      options.ipv4_lookup(true);
      break;
    case AF_INET6:
      options.ipv6_lookup(true);
      break;
    default:
      return EAI_FAMILY;
  }

  const fidl::WireResult fidl_result =
      name_lookup.value()->LookupIp(fidl::StringView::FromExternal(name), options.Build());
  if (!fidl_result.ok()) {
    errno = fdio_status_to_errno(fidl_result.status());
    return EAI_SYSTEM;
  }
  const auto* wire_result = fidl_result.Unwrap();
  if (wire_result->is_error()) {
    switch (wire_result->error_value()) {
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
  const fnet_name::wire::LookupResult& result = wire_result->value()->result;
  if (!result.has_addresses()) {
    return 0;
  }
  ZX_ASSERT_MSG(result.addresses().count() <= MAXADDRS,
                "%lu addresses in DNS response, maximum is %d", result.addresses().count(),
                MAXADDRS);
  int count = 0;
  for (const fnet::wire::IpAddress& addr : result.addresses()) {
    address& address = buf[count++];
    switch (addr.Which()) {
      case fnet::wire::IpAddress::Tag::kIpv4: {
        address = {
            .family = AF_INET,
        };
        const auto& octets = addr.ipv4().addr;
        static_assert(sizeof(address.addr) >= sizeof(octets));
        std::copy(octets.begin(), octets.end(), address.addr);
      } break;
      case fnet::wire::IpAddress::Tag::kIpv6: {
        // TODO(https://fxbug.dev/21415): Figure out a way to expose scope ID for IPv6
        // addresses.
        address = {
            .family = AF_INET6,
        };
        const auto& octets = addr.ipv6().addr;
        static_assert(sizeof(address.addr) >= sizeof(octets));
        std::copy(octets.begin(), octets.end(), address.addr);
      } break;
    }
  }
  return count;
}

__EXPORT
int getsockname(int fd, struct sockaddr* __restrict addr, socklen_t* __restrict len) {
  return delegate(fd, [&](const fdio_ptr& io, int16_t* out_code) {
    return zxio_getsockname(&io->zxio_storage().io, addr, len, out_code);
  });
}

__EXPORT
int getpeername(int fd, struct sockaddr* __restrict addr, socklen_t* __restrict len) {
  return delegate(fd, [&](const fdio_ptr& io, int16_t* out_code) {
    return zxio_getpeername(&io->zxio_storage().io, addr, len, out_code);
  });
}

__EXPORT
int getsockopt(int fd, int level, int optname, void* __restrict optval,
               socklen_t* __restrict optlen) {
  const fdio_ptr io = fd_to_io(fd);
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
  const zx_status_t status =
      zxio_getsockopt(&io->zxio_storage().io, level, optname, optval, optlen, &out_code);
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
  const fdio_ptr io = fd_to_io(fd);
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
  const zx_status_t status =
      zxio_setsockopt(&io->zxio_storage().io, level, optname, optval, optlen, &out_code);
  if (status != ZX_OK) {
    return ERROR(status);
  }
  if (out_code) {
    return ERRNO(out_code);
  }
  return 0;
}

namespace {

template <typename T, size_t N>
void populate_ifs(struct ifaddrs_storage* ifs, sa_family_t af, const fidl::Array<T, N>& addr_bytes,
                  uint64_t ifindex, uint8_t prefix_len) {
  copy_addr(&ifs->ifa.ifa_addr, af, &ifs->addr, const_cast<uint8_t*>(addr_bytes.data()),
            N * sizeof(T), static_cast<uint32_t>(ifindex));
  gen_netmask(&ifs->ifa.ifa_netmask, af, &ifs->netmask, prefix_len);
}

}  // namespace

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
  const zx_status_t status = response.status();
  if (status != ZX_OK) {
    return ERROR(status);
  }

  for (const auto& iface : response->interfaces) {
    if (!iface.has_id() || !iface.has_name() || !iface.has_addresses()) {
      continue;
    }

    const auto& if_id = iface.id();
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

      switch (addr.Which()) {
        case fnet::wire::IpAddress::Tag::kIpv4: {
          const auto& addr_bytes = addr.ipv4().addr;
          populate_ifs(ifs, AF_INET, addr_bytes, if_id, prefix_len);
          break;
        }
        case fnet::wire::IpAddress::Tag::kIpv6: {
          const auto& addr_bytes = addr.ipv6().addr;
          populate_ifs(ifs, AF_INET6, addr_bytes, if_id, prefix_len);
          break;
        }
      }

      if (iface.has_interface_flags()) {
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
  while (ifp) {
    ifaddrs* n = ifp->ifa_next;
    free(ifp);
    ifp = n;
  }
}

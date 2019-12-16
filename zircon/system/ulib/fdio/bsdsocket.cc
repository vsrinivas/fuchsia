// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/net/llcpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/io.h>
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

#include "private.h"
#include "unistd.h"

namespace fio = ::llcpp::fuchsia::io;
namespace fnet = ::llcpp::fuchsia::net;
namespace fsocket = ::llcpp::fuchsia::posix::socket;

#define MAKE_GET_SERVICE(fn_name, symbol)                        \
  static zx_status_t fn_name(symbol::SyncClient** out) {         \
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

MAKE_GET_SERVICE(get_socket_provider, fsocket::Provider)
MAKE_GET_SERVICE(get_name_lookup, fnet::NameLookup)

__EXPORT
int socket(int domain, int type, int protocol) {
  fsocket::Provider::SyncClient* provider;
  zx_status_t status = get_socket_provider(&provider);
  if (status != ZX_OK) {
    return ERRNO(EIO);
  }

  // We're going to manage blocking on the client side, so always ask the
  // provider for a non-blocking socket.
  auto socket_result =
      provider->Socket(static_cast<int16_t>(domain), static_cast<int16_t>(type) | SOCK_NONBLOCK,
                       static_cast<int16_t>(protocol));
  status = socket_result.status();
  if (status != ZX_OK) {
    return ERROR(status);
  }
  fsocket::Provider::SocketResponse* socket_response = socket_result.Unwrap();
  if (int16_t out_code = socket_response->code) {
    return ERRNO(out_code);
  }
  fdio_t* io;
  status = fdio_from_channel(std::move(socket_response->s), &io);
  if (status != ZX_OK) {
    return ERROR(status);
  }

  // TODO(tamird): we're not handling this flag in fdio_from_channel, which seems bad.
  if (type & SOCK_NONBLOCK) {
    *fdio_get_ioflag(io) |= IOFLAG_NONBLOCK;
  }

  // TODO(ZX-973): Implement CLOEXEC.
  // if (type & SOCK_CLOEXEC) {
  // }

  int fd = fdio_bind_to_fd(io, -1, 0);
  if (fd < 0) {
    fdio_get_ops(io)->close(io);
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
int accept4(int fd, struct sockaddr* __restrict addr, socklen_t* __restrict len, int flags) {
  if (flags & ~SOCK_NONBLOCK) {
    return ERRNO(EINVAL);
  }
  if ((addr == NULL) != (len == NULL)) {
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
      if ((status = fdio_get_ops(io)->accept(
               io, flags | SOCK_NONBLOCK, accepted.reset_and_get_address(), &out_code)) != ZX_OK) {
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

  if (len) {
    int16_t out_code;
    if ((status = fdio_get_ops(accepted_io)->getpeername(accepted_io, addr, len, &out_code)) !=
        ZX_OK) {
      fdio_release_reserved(nfd);
      fdio_get_ops(accepted_io)->close(accepted_io);
      fdio_release(accepted_io);
      return ERROR(status);
    }
    if (out_code) {
      fdio_release_reserved(nfd);
      fdio_get_ops(accepted_io)->close(accepted_io);
      fdio_release(accepted_io);
      return ERRNO(out_code);
    }
  }

  // TODO(tamird): we're not handling this flag in fdio_from_channel, which seems bad.
  if (flags & SOCK_NONBLOCK) {
    *fdio_get_ioflag(accepted_io) |= IOFLAG_NONBLOCK;
  }

  nfd = fdio_assign_reserved(nfd, accepted_io);
  if (nfd < 0) {
    fdio_get_ops(accepted_io)->close(accepted_io);
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
  auto result = name_lookup->LookupIp(request_buffer.view(), fidl::StringView(name, strlen(name)),
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
      buf[count].scopeid = 0;  // TODO(NET-2438): Figure out a way to expose scope ID
      auto addr = response.ipv6_addrs.at(i).addr;
      memcpy(buf[count].addr, addr.data(), addr.size());
      buf[count].sortkey = 0;
      count++;
    }
  }

  // TODO(NET-2437) support CNAME

  return count;
}
__EXPORT
int getsockname(int fd, struct sockaddr* __restrict addr, socklen_t* __restrict len) {
  if (len == NULL || addr == NULL) {
    return ERRNO(EINVAL);
  }

  return delegate(fd, [&](fdio_t* io, int16_t* out_code) {
    return fdio_get_ops(io)->getsockname(io, addr, len, out_code);
  });
}

__EXPORT
int getpeername(int fd, struct sockaddr* __restrict addr, socklen_t* __restrict len) {
  if (len == NULL || addr == NULL) {
    return ERRNO(EINVAL);
  }

  return delegate(fd, [&](fdio_t* io, int16_t* out_code) {
    return fdio_get_ops(io)->getpeername(io, addr, len, out_code);
  });
}

__EXPORT
int getsockopt(int fd, int level, int optname, void* __restrict optval,
               socklen_t* __restrict optlen) {
  if (optval == NULL || optlen == NULL) {
    return ERRNO(EFAULT);
  }

  fdio_t* io = fd_to_io(fd);
  if (io == nullptr) {
    return ERRNO(EBADF);
  }

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
      if (optlen == NULL || *optlen < sizeof(struct timeval)) {
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
  fdio_release(io);
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
  if (io == NULL) {
    return ERRNO(EBADF);
  }

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
          if (optval == NULL) {
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
          if (optval == NULL) {
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
  fdio_release(io);
  if (status != ZX_OK) {
    return ERROR(status);
  }
  if (out_code) {
    return ERRNO(out_code);
  }
  return 0;
}

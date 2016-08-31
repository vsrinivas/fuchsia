// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netdb.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "apps/netstack/net_socket.h"
#include "apps/netstack/ports/lwip/lwip-socket.h"
#include "apps/netstack/trace.h"

// Network Socket Layer for lwip

// TODO: IPv6

static void convert_sin_addr_to_lwip(const struct sockaddr_in* from,
                                     struct lwip_sockaddr_in* to) {
  memset(to, 0, sizeof(*to));
  to->sin_family = from->sin_family;
  to->sin_addr.s_addr = from->sin_addr.s_addr;
  to->sin_port = from->sin_port;
}

static void convert_sin_addr_from_lwip(const struct lwip_sockaddr_in* from,
                                       struct sockaddr_in* to) {
  memset(to, 0, sizeof(*to));
  to->sin_family = from->sin_family;
  to->sin_addr.s_addr = from->sin_addr.s_addr;
  to->sin_port = from->sin_port;
}

int net_socket(int domain, int type, int protocol) {
  if (type > 3) {
    error("net_socket: unknown type %d\n", type);
    errno = EIO;
    return -1;
  }
  return lwip_socket(domain, type, protocol);
}

int net_connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
  const struct sockaddr_in* addr_in = (const struct sockaddr_in*)addr;

  struct lwip_sockaddr_in lwip_addr_in;
  convert_sin_addr_to_lwip(addr_in, &lwip_addr_in);
  vdebug("net_connect: sin_family=%d\n", lwip_addr_in.sin_family);
  vdebug("net_connect: sin_addr=0x%x\n", lwip_addr_in.sin_addr.s_addr);
  vdebug("net_connect: sin_port=%d\n", lwip_addr_in.sin_port);

  return lwip_connect(sockfd, (struct lwip_sockaddr*)&lwip_addr_in,
                      sizeof(lwip_addr_in));
}

int net_bind(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
  const struct sockaddr_in* addr_in = (const struct sockaddr_in*)addr;

  struct lwip_sockaddr_in lwip_addr_in;
  convert_sin_addr_to_lwip(addr_in, &lwip_addr_in);
  vdebug("net_bind: sin_family=%d\n", lwip_addr_in.sin_family);
  vdebug("net_bind: sin_addr=%d\n", lwip_addr_in.sin_addr.s_addr);
  vdebug("net_bind: sin_port=%d\n", lwip_addr_in.sin_port);

  return lwip_bind(sockfd, (struct lwip_sockaddr*)&lwip_addr_in,
                   sizeof(lwip_addr_in));
}

int net_listen(int sockfd, int backlog) { return lwip_listen(sockfd, backlog); }

int net_accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
  struct lwip_sockaddr_in lwip_addr_in;
  lwip_socklen_t lwip_addrlen = sizeof(lwip_addr_in);

  int ret;
  if (addr == NULL && addrlen == NULL) {
    ret = lwip_accept(sockfd, NULL, NULL);
  } else if (addr == NULL || addrlen == NULL) {
    errno = EINVAL;
    return -1;
  } else {
    ret = lwip_accept(sockfd, (struct lwip_sockaddr*)&lwip_addr_in,
                      &lwip_addrlen);
    if (ret < 0) return ret;
    // TODO: IPv6
    if (*addrlen < sizeof(struct sockaddr_in)) {
      errno = EINVAL;
      return -1;
    }
    vdebug("net_accept: sin_family=%d\n", lwip_addr_in.sin_family);
    vdebug("net_accept: sin_addr=%d\n", lwip_addr_in.sin_addr.s_addr);
    vdebug("net_accept: sin_port=%d\n", lwip_addr_in.sin_port);

    convert_sin_addr_from_lwip(&lwip_addr_in, (struct sockaddr_in*)addr);
    *addrlen = sizeof(struct sockaddr_in);
  }
  return ret;
}

ssize_t net_read(int sockfd, void* buf, size_t count) {
  return lwip_read(sockfd, buf, count);
}

ssize_t net_write(int sockfd, const void* buf, size_t count) {
  return lwip_write(sockfd, buf, count);
}

int net_getsockopt(int sockfd, int level, int optname, void* optval,
                   socklen_t* optlen) {
  int lwip_level;
  int lwip_optname;
  switch (level) {
    case SOL_SOCKET:
      lwip_level = 0xfff;
      break;
    default:
      error("net_getsockopt: unknown level %d\n", level);
      errno = EINVAL;
      return -1;
  }
  switch (optname) {
    case SO_ERROR:
      lwip_optname = 0x1007;
      break;
    case SO_REUSEADDR:
      lwip_optname = 0x0004;
      break;
    case SO_KEEPALIVE:
      lwip_optname = 0x0008;
      break;
    case SO_BROADCAST:
      lwip_optname = 0x0020;
      break;
    default:
      error("net_getsockopt: unknown optname %d\n", optname);
      errno = EINVAL;
      return -1;
  }
  return lwip_getsockopt(sockfd, lwip_level, lwip_optname, optval, optlen);
}

int net_setsockopt(int sockfd, int level, int optname, const void* optval,
                   socklen_t optlen) {
  int lwip_level;
  int lwip_optname;
  switch (level) {
    case SOL_SOCKET:
      lwip_level = 0xfff;
      break;
    default:
      error("net_setsockopt: unknown level %d\n", level);
      errno = EINVAL;
      return -1;
  }
  switch (optname) {
    case SO_ERROR:
      lwip_optname = 0x1007;
      break;
    case SO_REUSEADDR:
      lwip_optname = 0x0004;
      break;
    case SO_KEEPALIVE:
      lwip_optname = 0x0008;
      break;
    case SO_BROADCAST:
      lwip_optname = 0x0020;
      break;
    default:
      error("net_setsockopt: unknown optname %d\n", optname);
      errno = EINVAL;
      return -1;
  }
  return lwip_setsockopt(sockfd, lwip_level, lwip_optname, optval, optlen);
}

int net_getpeername(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
  struct lwip_sockaddr_in lwip_addr_in;
  lwip_socklen_t lwip_addrlen = sizeof(lwip_addr_in);

  int ret;
  if (addr == NULL || addrlen == NULL) {
    errno = EINVAL;
    return -1;
  } else {
    ret = lwip_getpeername(sockfd, (struct lwip_sockaddr*)&lwip_addr_in,
                           &lwip_addrlen);
    if (ret < 0) return ret;
    // TODO: IPv6
    if (*addrlen < sizeof(struct sockaddr_in)) {
      errno = EINVAL;
      return -1;
    }
    convert_sin_addr_from_lwip(&lwip_addr_in, (struct sockaddr_in*)addr);
    *addrlen = sizeof(struct sockaddr_in);
  }
  return 0;
}

int net_getsockname(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
  struct lwip_sockaddr_in lwip_addr_in;
  lwip_socklen_t lwip_addrlen = sizeof(lwip_addr_in);

  int ret;
  if (addr == NULL || addrlen == NULL) {
    errno = EINVAL;
    return -1;
  } else {
    ret = lwip_getsockname(sockfd, (struct lwip_sockaddr*)&lwip_addr_in,
                           &lwip_addrlen);
    if (ret < 0) return ret;
    // TODO: IPv6
    if (*addrlen < sizeof(struct sockaddr_in)) {
      errno = EINVAL;
      return -1;
    }
    convert_sin_addr_from_lwip(&lwip_addr_in, (struct sockaddr_in*)addr);
    *addrlen = sizeof(struct sockaddr_in);
  }
  return 0;
}

int net_ioctl(int sockfd, int request, ...) {
  va_list args;
  va_start(args, request);
  void* argp = va_arg(args, void*);
  va_end(args);

  int lwip_request;
  switch (request) {
    case FIONBIO:
      lwip_request = 0x8008667e;  // TODO: better way?
      break;
    case FIONREAD:
      lwip_request = 0x4008667f;  // TODO: better way?
      break;
    default:
      error("net_ioctl: unknown request %x\n", request);
      errno = EINVAL;
      return -1;
  }
  return lwip_ioctl(sockfd, lwip_request, argp);
}

int net_close(int sockfd) { return lwip_close(sockfd); }

int net_getaddrinfo(const char* node, const char* service,
                    const struct addrinfo* hints, struct addrinfo** res_p) {
  int ret;
  struct addrinfo* lwip_res;

  if (res_p == NULL) {
    errno = EINVAL;
    return EAI_SYSTEM;
  }

  ret = lwip_getaddrinfo(node, service, hints, &lwip_res);
  if (ret != 0) return ret;

  // TODO: may need to map errno

  // TODO: we are returning the first one only
  struct addrinfo* res = malloc(sizeof(struct addrinfo));
  assert(res);
  res->ai_flags = lwip_res->ai_flags;
  res->ai_family = lwip_res->ai_family;
  res->ai_socktype = lwip_res->ai_socktype;
  res->ai_flags = lwip_res->ai_flags;
  if (lwip_res->ai_addr == NULL) {
    res->ai_addrlen = 0;
    res->ai_addr = NULL;
  } else {
    res->ai_addrlen = sizeof(struct sockaddr_in);
    struct sockaddr_in* sockaddr_in = malloc(sizeof(struct sockaddr_in));
    assert(sockaddr_in);
    convert_sin_addr_from_lwip(
        (const struct lwip_sockaddr_in*)lwip_res->ai_addr, sockaddr_in);
    res->ai_addr = (struct sockaddr*)sockaddr_in;
  }
  res->ai_canonname = NULL;  // TODO
  res->ai_next = NULL;       // TODO

  lwip_freeaddrinfo(lwip_res);

  *res_p = res;
  return 0;
}

int net_freeaddrinfo(struct addrinfo* res) {
  if (res == NULL) {
    errno = EINVAL;
    return EAI_SYSTEM;
  }
  do {
    free(res->ai_addr);
    struct addrinfo* next = res->ai_next;
    free(res);
    res = next;
  } while (res != NULL);
  return 0;
}

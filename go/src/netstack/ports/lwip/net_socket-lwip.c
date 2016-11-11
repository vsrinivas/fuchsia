// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "apps/netstack/net_socket.h"
#include "apps/netstack/ports/lwip/lwip-netdb.h"
#include "apps/netstack/ports/lwip/lwip-socket.h"
#include "apps/netstack/trace.h"

// Network Socket Layer for lwip

static void convert_sin_addr_to_lwip(const struct sockaddr_in* from,
                                     struct lwip_sockaddr_in* to) {
  memset(to, 0, sizeof(*to));
  to->sin_family = from->sin_family;
  to->sin_port = from->sin_port;
  to->sin_addr.s_addr = from->sin_addr.s_addr;

  debug_port("sin_family=%d\n", to->sin_family);
  char str[INET_ADDRSTRLEN];
  debug_port("sin_addr=%s\n",
             inet_ntop(AF_INET, &to->sin_addr, str, INET_ADDRSTRLEN));
  debug_port("sin_port=%d\n", to->sin_port);
}

static void convert_sin_addr_from_lwip(const struct lwip_sockaddr_in* from,
                                       struct sockaddr_in* to) {
  memset(to, 0, sizeof(*to));
  to->sin_family = from->sin_family;
  to->sin_port = from->sin_port;
  to->sin_addr.s_addr = from->sin_addr.s_addr;

  debug_port("sin_family=%d\n", to->sin_family);
  char str[INET_ADDRSTRLEN];
  debug_port("sin_addr=%s\n",
             inet_ntop(AF_INET, &to->sin_addr, str, INET_ADDRSTRLEN));
  debug_port("sin_port=%d\n", to->sin_port);
}

static void convert_sin6_addr_to_lwip(const struct sockaddr_in6* from,
                                      struct lwip_sockaddr_in6* to) {
  memset(to, 0, sizeof(*to));
  to->sin6_family = from->sin6_family;
  to->sin6_port = from->sin6_port;
  to->sin6_flowinfo = from->sin6_flowinfo;
  to->sin6_addr = from->sin6_addr;
  to->sin6_scope_id = from->sin6_scope_id;

  debug_port("sin6_family=%d\n", to->sin6_family);
  char str[INET6_ADDRSTRLEN];
  debug_port("sin6_addr=%s\n",
             inet_ntop(AF_INET6, &to->sin6_addr, str, INET6_ADDRSTRLEN));
  debug_port("sin6_port=%d\n", to->sin6_port);
}

static void convert_sin6_addr_from_lwip(const struct lwip_sockaddr_in6* from,
                                        struct sockaddr_in6* to) {
  memset(to, 0, sizeof(*to));
  to->sin6_family = from->sin6_family;
  to->sin6_port = from->sin6_port;
  to->sin6_flowinfo = from->sin6_flowinfo;
  to->sin6_addr = from->sin6_addr;
  to->sin6_scope_id = from->sin6_scope_id;

  debug_port("sin6_family=%d\n", to->sin6_family);
  char str[INET6_ADDRSTRLEN];
  debug_port("sin6_addr=%s\n",
             inet_ntop(AF_INET6, &to->sin6_addr, str, INET6_ADDRSTRLEN));
  debug_port("sin6_port=%d\n", to->sin6_port);
}

static int convert_addr_to_lwip(const struct sockaddr* from, socklen_t from_len,
                                struct lwip_sockaddr* to,
                                lwip_socklen_t* to_len) {
  if (from->sa_family == AF_INET) {
    if (from_len < sizeof(struct sockaddr_in) ||
        *to_len < sizeof(struct lwip_sockaddr_in)) {
      debug_port("from_len(%d) < %lu || to_len(%d) < %lu\n", from_len,
                 sizeof(struct sockaddr_in), *to_len,
                 sizeof(struct lwip_sockaddr_in));
      return -1;
    }
    convert_sin_addr_to_lwip((const struct sockaddr_in*)from,
                             (struct lwip_sockaddr_in*)to);
    *to_len = sizeof(struct lwip_sockaddr_in);
  } else if (from->sa_family == AF_INET6) {
    if (from_len < sizeof(struct sockaddr_in6) ||
        *to_len < sizeof(struct lwip_sockaddr_in6)) {
      debug_port("from_len(%d) < %lu || to_len(%d) < %lu\n", from_len,
                 sizeof(struct sockaddr_in6), *to_len,
                 sizeof(struct lwip_sockaddr_in6));
      return -1;
    }
    convert_sin6_addr_to_lwip((const struct sockaddr_in6*)from,
                              (struct lwip_sockaddr_in6*)to);
    *to_len = sizeof(struct lwip_sockaddr_in6);
  } else {
    debug_port("unknown family(%d)\n", from->sa_family);
    return -1;
  }
  return 0;
}

static int convert_addr_from_lwip(const struct lwip_sockaddr* from,
                                  lwip_socklen_t from_len, struct sockaddr* to,
                                  socklen_t* to_len) {
  if (from->sa_family == AF_INET) {
    if (from_len < sizeof(struct lwip_sockaddr_in) ||
        *to_len < sizeof(struct sockaddr_in)) {
      debug_port("from_len(%d) < %lu || to_len(%d) < %lu\n", from_len,
                 sizeof(struct lwip_sockaddr_in), *to_len,
                 sizeof(struct sockaddr_in));
      return -1;
    }
    convert_sin_addr_from_lwip((const struct lwip_sockaddr_in*)from,
                               (struct sockaddr_in*)to);
    *to_len = sizeof(struct sockaddr_in);
  } else if (from->sa_family == AF_INET6) {
    if (from_len < sizeof(struct lwip_sockaddr_in6) ||
        *to_len < sizeof(struct sockaddr_in6)) {
      debug_port("from_len(%d) < %lu || to_len(%d) < %lu\n", from_len,
                 sizeof(struct lwip_sockaddr_in6), *to_len,
                 sizeof(struct sockaddr_in6));
      return -1;
    }
    convert_sin6_addr_from_lwip((const struct lwip_sockaddr_in6*)from,
                                (struct sockaddr_in6*)to);
    *to_len = sizeof(struct sockaddr_in6);
  } else {
    debug_port("unknown family(%d)\n", from->sa_family);
    return -1;
  }
  return 0;
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
  struct lwip_sockaddr_storage lwip_addr;
  lwip_socklen_t lwip_addrlen = sizeof(lwip_addr);
  if (convert_addr_to_lwip(addr, addrlen, (struct lwip_sockaddr*)&lwip_addr,
                           &lwip_addrlen) < 0) {
    errno = EINVAL;
    return -1;
  }
  return lwip_connect(sockfd, (struct lwip_sockaddr*)&lwip_addr, lwip_addrlen);
}

int net_bind(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
  struct lwip_sockaddr_storage lwip_addr;
  lwip_socklen_t lwip_addrlen = sizeof(lwip_addr);
  if (convert_addr_to_lwip(addr, addrlen, (struct lwip_sockaddr*)&lwip_addr,
                           &lwip_addrlen) < 0) {
    errno = EINVAL;
    return -1;
  }
  return lwip_bind(sockfd, (struct lwip_sockaddr*)&lwip_addr, lwip_addrlen);
}

int net_listen(int sockfd, int backlog) { return lwip_listen(sockfd, backlog); }

int net_accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
  int ret;
  if (addr == NULL && addrlen == NULL) {
    ret = lwip_accept(sockfd, NULL, NULL);
  } else if (addr == NULL || addrlen == NULL) {
    errno = EINVAL;
    return -1;
  } else {
    struct lwip_sockaddr_storage lwip_addr;
    lwip_socklen_t lwip_addrlen = sizeof(lwip_addr);
    ret = lwip_accept(sockfd, (struct lwip_sockaddr*)&lwip_addr, &lwip_addrlen);
    if (ret < 0) return ret;  // errno is propagated
    if (convert_addr_from_lwip((const struct lwip_sockaddr*)&lwip_addr,
                               lwip_addrlen, addr, addrlen) < 0) {
      errno = EINVAL;
      return -1;
    }
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
  debug_port("net_getsockopt: level=%d optname=%d *optlen=%d\n", level, optname,
             *optlen);
  // lwip_getsockopt() doesn't adjust optlen so we have to do it here
  switch (optname) {
    case SO_ERROR:
      lwip_optname = 0x1007;
      *optlen = sizeof(int);
      break;
    case SO_REUSEADDR:
      lwip_optname = 0x0004;
      *optlen = sizeof(int);
      break;
    case SO_KEEPALIVE:
      lwip_optname = 0x0008;
      *optlen = sizeof(int);
      break;
    case SO_BROADCAST:
      lwip_optname = 0x0020;
      *optlen = sizeof(int);
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
  struct lwip_sockaddr_storage lwip_addr;
  lwip_socklen_t lwip_addrlen = sizeof(lwip_addr);

  int ret;
  if (addr == NULL || addrlen == NULL) {
    errno = EINVAL;
    return -1;
  } else {
    ret = lwip_getpeername(sockfd, (struct lwip_sockaddr*)&lwip_addr,
                           &lwip_addrlen);
    if (ret < 0) return ret;
    if (convert_addr_from_lwip((const struct lwip_sockaddr*)&lwip_addr,
                               lwip_addrlen, addr, addrlen) < 0) {
      errno = EINVAL;
      return -1;
    }
  }
  return 0;
}

int net_getsockname(int sockfd, struct sockaddr* addr, socklen_t* addrlen) {
  struct lwip_sockaddr_storage lwip_addr;
  lwip_socklen_t lwip_addrlen = sizeof(lwip_addr);

  int ret;
  if (addr == NULL || addrlen == NULL) {
    errno = EINVAL;
    return -1;
  } else {
    ret = lwip_getsockname(sockfd, (struct lwip_sockaddr*)&lwip_addr,
                           &lwip_addrlen);
    if (ret < 0) return ret;
    convert_addr_from_lwip((const struct lwip_sockaddr*)&lwip_addr,
                           lwip_addrlen, addr, addrlen);
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

static int convert_gai_error(int lwip_error) {
  switch (lwip_error) {
    case LWIP_EAI_NONAME:
      return EAI_NONAME;
    case LWIP_EAI_SERVICE:
      return EAI_SERVICE;
    case LWIP_EAI_FAIL:
      return EAI_FAIL;
    case LWIP_EAI_MEMORY:
      return EAI_MEMORY;
    case LWIP_EAI_FAMILY:
      return EAI_FAMILY;
    case LWIP_HOST_NOT_FOUND:
      return HOST_NOT_FOUND;
    case LWIP_NO_DATA:
      return NO_DATA;
    case LWIP_NO_RECOVERY:
      return NO_RECOVERY;
    case LWIP_TRY_AGAIN:
      return TRY_AGAIN;
    default:  // impossible
      return EAI_FAIL;
  }
}

int net_getaddrinfo(const char* node, const char* service,
                    const struct addrinfo* hints, struct addrinfo** res_p) {
  int ret;
  struct addrinfo* lwip_res;

  if (res_p == NULL) {
    errno = EINVAL;
    return EAI_SYSTEM;
  }

  // TODO: do this somewhere else
  if (strcmp(service, "http") == 0) {
    service = "80";
  } else if (strcmp(service, "https") == 0) {
    service = "443";
  }

  ret = lwip_getaddrinfo(node, service, hints, &lwip_res);
  if (ret != 0) {
    return convert_gai_error(ret);
  }

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
    struct sockaddr* addr = malloc(sizeof(struct sockaddr_storage));
    assert(addr);
    socklen_t addrlen = sizeof(struct sockaddr_storage);

    if (convert_addr_from_lwip((const struct lwip_sockaddr*)lwip_res->ai_addr,
                               lwip_res->ai_addrlen, addr, &addrlen) < 0) {
      free(addr);
      errno = EINVAL;
      return EAI_SYSTEM;
    }
    res->ai_addr = addr;
    res->ai_addrlen = addrlen;
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

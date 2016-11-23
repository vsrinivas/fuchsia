// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_NETSTACK_PORTS_LWIP_LWIP_SOCKET_H_
#define APPS_NETSTACK_PORTS_LWIP_LWIP_SOCKET_H_

// The definitions here are identical to the ones in lwip/sockets.h
// except the "lwip_" prefix.

typedef uint8_t lwip_sa_family_t;
typedef uint16_t lwip_in_port_t;
typedef uint32_t lwip_in_addr_t;
typedef uint32_t lwip_socklen_t;

struct lwip_in_addr {
  lwip_in_addr_t s_addr;
};

struct lwip_sockaddr_in {
  uint8_t sin_len;
  lwip_sa_family_t sin_family;
  lwip_in_port_t sin_port;
  struct lwip_in_addr sin_addr;
  int8_t sin_zero[8];
};

struct lwip_sockaddr_in6 {
  uint8_t sin6_len;
  lwip_sa_family_t sin6_family;
  lwip_in_port_t sin6_port;
  uint32_t sin6_flowinfo;
  struct in6_addr sin6_addr;
  uint32_t sin6_scope_id;
};

struct lwip_sockaddr {
  uint8_t sa_len;
  lwip_sa_family_t sa_family;
  char sa_data[14];
};

struct lwip_sockaddr_storage {
  uint8_t s2_len;
  lwip_sa_family_t ss_family;
  char s2_data1[2];
  uint32_t s2_data2[3];
  uint32_t s2_data3[3];
};

int lwip_socket(int domain, int type, int protocol);
int lwip_connect(int sockfd, struct lwip_sockaddr* addr,
                 lwip_socklen_t addrlen);
int lwip_bind(int sockfd, struct lwip_sockaddr* addr, lwip_socklen_t addrlen);
int lwip_listen(int sockfd, int backlog);
int lwip_accept(int sockfd, struct lwip_sockaddr* addr,
                lwip_socklen_t* addrlen);
int lwip_read(int sockfd, void* buf, size_t count);
int lwip_write(int sockfd, const void* buf, size_t count);
int lwip_getsockopt(int sockfd, int level, int optname, void* optval,
                    socklen_t* option);
int lwip_setsockopt(int sockfd, int level, int optname, const void* optval,
                    socklen_t option);
int lwip_getpeername(int sockfd, struct lwip_sockaddr* addr,
                     lwip_socklen_t* addrlen);
int lwip_getsockname(int sockfd, struct lwip_sockaddr* addr,
                     lwip_socklen_t* addrlen);
int lwip_ioctl(int sockfd, int request, void* argp);
int lwip_close(int sockfd);
int lwip_shutdown(int sockfd, int how);

int lwip_getaddrinfo(const char* node, const char* service,
                     const struct addrinfo* hints, struct addrinfo** res);
int lwip_freeaddrinfo(struct addrinfo* res);

#endif  // APPS_NETSTACK_PORTS_LWIP_LWIP_SOCKET_H_

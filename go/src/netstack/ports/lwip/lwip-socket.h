// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_NETSTACK_PORTS_LWIP_LWIP_SOCKET_H_
#define APPS_NETSTACK_PORTS_LWIP_LWIP_SOCKET_H_

// options for socket level
#define LWIP_SOL_SOCKET  0xfff

#define LWIP_SO_REUSEADDR   0x0004
#define LWIP_SO_KEEPALIVE   0x0008
#define LWIP_SO_BROADCAST   0x0020

#define LWIP_SO_DEBUG       0x0001
#define LWIP_SO_ACCEPTCONN  0x0002
#define LWIP_SO_DONTROUTE   0x0010
#define LWIP_SO_USELOOPBACK 0x0040
#define LWIP_SO_LINGER      0x0080
#define LWIP_SO_DONTLINGER  ((int)(~LWIP_SO_LINGER))
#define LWIP_SO_OOBINLINE   0x0100
#define LWIP_SO_REUSEPORT   0x0200
#define LWIP_SO_SNDBUF      0x1001
#define LWIP_SO_RCVBUF      0x1002
#define LWIP_SO_SNDLOWAT    0x1003
#define LWIP_SO_RCVLOWAT    0x1004
#define LWIP_SO_SNDTIMEO    0x1005
#define LWIP_SO_RCVTIMEO    0x1006
#define LWIP_SO_ERROR       0x1007
#define LWIP_SO_TYPE        0x1008
#define LWIP_SO_CONTIMEO    0x1009
#define LWIP_SO_NO_CHECK    0x100a

// options for IP level
#define LWIP_IP_TOS 1
#define LWIP_IP_TTL 2
#define LWIP_IP_ADD_MEMBERSHIP 3
#define LWIP_IP_DROP_MEMBERSHIP 4
#define LWIP_IP_MULTICAST_TTL 5
#define LWIP_IP_MULTICAST_IF 6
#define LWIP_IP_MULTICAST_LOOP 7

// options for TCP level
#define LWIP_TCP_NODELAY    0x01

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

typedef struct {
  char name[16];  // null-terminated
  struct lwip_sockaddr addr;
  struct lwip_sockaddr netmask;
  struct lwip_sockaddr broadaddr;
  uint32_t flags;
  uint16_t index;
  uint16_t hwaddr_len;
  uint8_t hwaddr[8];
} lwip_net_if_info_t;

int lwip_socket(int domain, int type, int protocol);
int lwip_connect(int sockfd, struct lwip_sockaddr* addr,
                 lwip_socklen_t addrlen);
int lwip_bind(int sockfd, struct lwip_sockaddr* addr, lwip_socklen_t addrlen);
int lwip_listen(int sockfd, int backlog);
int lwip_accept(int sockfd, struct lwip_sockaddr* addr,
                lwip_socklen_t* addrlen);
int lwip_read(int sockfd, void* buf, size_t count);
int lwip_write(int sockfd, const void* buf, size_t count);
int lwip_recvfrom(int sockfd, void* buf, size_t count, int flags,
                  struct lwip_sockaddr* addr, lwip_socklen_t* addrlen);
int lwip_sendto(int sockfd, const void* buf, size_t count, int flags,
                const struct lwip_sockaddr* addr, lwip_socklen_t addrlen);
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

int lwip_net_get_if_info(int index, lwip_net_if_info_t* info);
int lwip_net_set_if_addr_v4(const char* ifname,
                            const struct lwip_sockaddr* ipaddr,
                            const struct lwip_sockaddr* netmask);
int lwip_net_get_if_gateway_v4(const char* ifname,
                               struct lwip_sockaddr* gateway);
int lwip_net_set_if_gateway_v4(const char* ifname,
                               const struct lwip_sockaddr* gateway);
int lwip_net_get_dhcp_status_v4(const char* ifname, int* dhcp_status);
int lwip_net_set_dhcp_status_v4(const char* ifname, const int dhcp_status);
int lwip_net_get_dns_server_v4(struct lwip_sockaddr* dns_server);
int lwip_net_set_dns_server_v4(const struct lwip_sockaddr* dns_server);

#endif  // APPS_NETSTACK_PORTS_LWIP_LWIP_SOCKET_H_

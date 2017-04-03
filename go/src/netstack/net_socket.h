// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_NETSTACK_NETSOCKET_H_
#define APPS_NETSTACK_NETSOCKET_H_

int net_socket(int domain, int type, int protocol);
int net_connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen);
int net_bind(int sockfd, const struct sockaddr* addr, socklen_t addrlen);
int net_listen(int sockfd, int backlog);
int net_accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen);
ssize_t net_read(int sockfd, void* buf, size_t count);
ssize_t net_write(int sockfd, const void* buf, size_t count);
ssize_t net_recvfrom(int sockfd, void* buf, size_t count, int flags,
                     struct sockaddr* addr, socklen_t* addrlen);
ssize_t net_sendto(int sockfd, const void* buf, size_t count, int flags,
                   const struct sockaddr* addr, socklen_t addrlen);
int net_getsockopt(int sockfd, int level, int optname, void* optval,
                   socklen_t* optlen);
int net_setsockopt(int sockfd, int level, int optname, const void* optval,
                   socklen_t optlen);
int net_getpeername(int sockfd, struct sockaddr* addr, socklen_t* addrlen);
int net_getsockname(int sockfd, struct sockaddr* addr, socklen_t* addrlen);
int net_ioctl(int sockfd, int request, ...);
int net_close(int sockfd);
int net_shutdown(int sockfd, int how);
int net_getaddrinfo(const char* node, const char* service,
                    const struct addrinfo* sys_hints,
                    struct addrinfo** sys_res_p);
int net_freeaddrinfo(struct addrinfo* res);

typedef struct {
  char name[16];  // null-terminated
  struct sockaddr_storage addr;
  struct sockaddr_storage netmask;
  struct sockaddr_storage broadaddr;
  uint32_t flags;
  uint16_t index;
  uint16_t hwaddr_len;
  uint8_t hwaddr[8];
} net_if_info_t;

int net_get_if_info(int index, net_if_info_t* info);
int net_set_if_addr_v4(const char* ifname,
                       const struct sockaddr* ipaddr,
                       const struct sockaddr* netmask);
int net_get_if_gateway_v4(const char* ifname, struct sockaddr* gateway);
int net_set_if_gateway_v4(const char* ifname, const struct sockaddr* gateway);
int net_get_dhcp_status_v4(const char* ifname, int* dhcp_status);
int net_set_dhcp_status_v4(const char* ifname, const int dhcp_status);
int net_get_dns_server_v4(struct sockaddr* dns_server);
int net_set_dns_server_v4(const struct sockaddr* dns_server);

#endif  // APPS_NETSTACK_NETSOCKET_H_

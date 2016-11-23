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

#endif  // APPS_NETSTACK_NETSOCKET_H_

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_NETWORK_TOOLS_SOCKSCRIPTER_API_ABSTRACTION_H_
#define SRC_CONNECTIVITY_NETWORK_TOOLS_SOCKSCRIPTER_API_ABSTRACTION_H_

class ApiAbstraction {
 public:
  virtual int socket(int domain, int type, int protocol) = 0;
  virtual int close(int fd) = 0;
  virtual int setsockopt(int fd, int level, int optname, const void* optval, socklen_t optlen) = 0;
  virtual int getsockopt(int fd, int level, int optname, void* optval, socklen_t* optlen) = 0;
  virtual int bind(int fd, const struct sockaddr* addr, socklen_t len) = 0;
  virtual int connect(int fd, const struct sockaddr* addr, socklen_t len) = 0;
  virtual int accept(int fd, struct sockaddr* addr, socklen_t* len) = 0;
  virtual int listen(int fd, int backlog) = 0;
  virtual ssize_t send(int fd, const void* buf, size_t len, int flags) = 0;
  virtual ssize_t sendto(int fd, const void* buf, size_t buflen, int flags,
                         const struct sockaddr* addr, socklen_t addrlen) = 0;
  virtual ssize_t recv(int fd, void* buf, size_t len, int flags) = 0;
  virtual ssize_t recvfrom(int fd, void* buf, size_t buflen, int flags, struct sockaddr* addr,
                           socklen_t* addrlen) = 0;
  virtual int getsockname(int fd, struct sockaddr* addr, socklen_t* len) = 0;
  virtual int getpeername(int fd, struct sockaddr* addr, socklen_t* len) = 0;
};

#endif  // SRC_CONNECTIVITY_NETWORK_TOOLS_SOCKSCRIPTER_API_ABSTRACTION_H_

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests ensure the zircon libc can talk to netstack.
// No network connection is required, only a running netstack binary.

#include <netinet/in.h>
#include <sys/socket.h>

#include "gtest/gtest.h"

TEST(AcceptTest, Localhost) {
  int serverfd = socket(AF_INET6, SOCK_STREAM, 0);
  ASSERT_GE(serverfd, 0) << strerror(errno);

  struct sockaddr_in6 serveraddr = {};
  serveraddr.sin6_family = AF_INET6;
  serveraddr.sin6_addr = IN6ADDR_LOOPBACK_INIT;
  socklen_t serveraddrlen = sizeof(serveraddr);
  ASSERT_EQ(bind(serverfd, (sockaddr*)&serveraddr, serveraddrlen), 0)
      << strerror(errno);
  ASSERT_EQ(getsockname(serverfd, (sockaddr*)&serveraddr, &serveraddrlen), 0)
      << strerror(errno);
  ASSERT_EQ(serveraddrlen, sizeof(serveraddr));
  ASSERT_EQ(listen(serverfd, 1), 0) << strerror(errno);

  int clientfd = socket(AF_INET6, SOCK_STREAM, 0);
  ASSERT_GE(clientfd, 0) << strerror(errno);
  ASSERT_EQ(connect(clientfd, (sockaddr*)&serveraddr, serveraddrlen), 0)
      << strerror(errno);

  struct sockaddr_in connaddr;
  socklen_t connaddrlen = sizeof(connaddr);
  int connfd = accept(serverfd, (sockaddr*)&connaddr, &connaddrlen);
  ASSERT_GE(connfd, 0) << strerror(errno);
  ASSERT_GT(connaddrlen, sizeof(connaddr));
}

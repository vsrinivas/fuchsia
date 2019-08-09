// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests should run without any network interface (except loopback).

#include <arpa/inet.h>
#include <limits.h>
#include <sys/utsname.h>

#include "gtest/gtest.h"

namespace {

TEST(NoNetworkTest, NonBlockingConnectHostV4) {
  int connfd;
  ASSERT_GE(connfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0), 0) << strerror(errno);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  ASSERT_EQ(inet_pton(AF_INET, "192.168.0.1", &addr.sin_addr), 1) << strerror(errno);
  addr.sin_port = htons(10000);

  ASSERT_EQ(connect(connfd, (const struct sockaddr*)&addr, sizeof(addr)), -1);
  ASSERT_EQ(errno, EHOSTUNREACH) << strerror(errno);

  ASSERT_EQ(close(connfd), 0) << strerror(errno);
}

TEST(NoNetworkTest, NonBlockingConnectHostV6) {
  int connfd;
  ASSERT_GE(connfd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0), 0) << strerror(errno);

  struct sockaddr_in6 addr;
  addr.sin6_family = AF_INET6;
  ASSERT_EQ(inet_pton(AF_INET6, "fc00::1", &addr.sin6_addr), 1) << strerror(errno);
  addr.sin6_port = htons(10000);

  ASSERT_EQ(connect(connfd, (const struct sockaddr*)&addr, sizeof(addr)), -1);
  ASSERT_EQ(errno, EHOSTUNREACH) << strerror(errno);

  ASSERT_EQ(close(connfd), 0) << strerror(errno);
}

TEST(NoNetworkTest, NonBlockingConnectNetV4) {
  int connfd;
  ASSERT_GE(connfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0), 0) << strerror(errno);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  // multicast address
  ASSERT_EQ(inet_pton(AF_INET, "224.0.0.0", &addr.sin_addr), 1) << strerror(errno);
  addr.sin_port = htons(10000);

  ASSERT_EQ(connect(connfd, (const struct sockaddr*)&addr, sizeof(addr)), -1);
  ASSERT_EQ(errno, ENETUNREACH) << strerror(errno);

  ASSERT_EQ(close(connfd), 0) << strerror(errno);
}

TEST(NoNetworkTest, NonBlockingConnectNetV6) {
  int connfd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0);
  ASSERT_GE(connfd, 0) << "socket failed: " << strerror(errno);

  struct sockaddr_in6 addr;
  addr.sin6_family = AF_INET6;
  // linklocal address
  ASSERT_EQ(inet_pton(AF_INET6, "fe80::1", &addr.sin6_addr), 1) << strerror(errno);
  addr.sin6_port = htons(10000);

  ASSERT_EQ(connect(connfd, (const struct sockaddr*)&addr, sizeof(addr)), -1);
  ASSERT_EQ(errno, ENETUNREACH) << strerror(errno);

  ASSERT_EQ(close(connfd), 0) << strerror(errno);
}

}  // namespace

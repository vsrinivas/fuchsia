// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests should run without any network interface (except loopback).

#include <arpa/inet.h>

#include "gtest/gtest.h"

namespace {

TEST(NoNetworkTest, NonBlockingConnectHostV4) {
  int connfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  ASSERT_GE(connfd, 0) << "socket failed: " << strerror(errno);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  inet_pton(AF_INET, "192.168.0.1", &addr.sin_addr);
  addr.sin_port = htons(10000);

  int ret = connect(connfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(-1, ret);
  ASSERT_EQ(EHOSTUNREACH, errno) << "connect failed: " << strerror(errno);

  ASSERT_EQ(0, close(connfd)) << "close failed: " << strerror(errno);
}

TEST(NoNetworkTest, NonBlockingConnectHostV6) {
  int connfd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0);
  ASSERT_GE(connfd, 0) << "socket failed: " << strerror(errno);

  struct sockaddr_in6 addr;
  addr.sin6_family = AF_INET6;
  inet_pton(AF_INET6, "fc00::1", &addr.sin6_addr);
  addr.sin6_port = htons(10000);

  int ret = connect(connfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(-1, ret);
  ASSERT_EQ(EHOSTUNREACH, errno) << "connect failed: " << strerror(errno);

  ASSERT_EQ(0, close(connfd)) << "close failed: " << strerror(errno);
}

TEST(NoNetworkTest, NonBlockingConnectNetV4) {
  int connfd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
  ASSERT_GE(connfd, 0) << "socket failed: " << strerror(errno);

  struct sockaddr_in addr;
  addr.sin_family = AF_INET;
  inet_pton(AF_INET, "224.0.0.0", &addr.sin_addr);  // multicast address
  addr.sin_port = htons(10000);

  int ret = connect(connfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(-1, ret);
  ASSERT_EQ(ENETUNREACH, errno) << "connect failed: " << strerror(errno);

  ASSERT_EQ(0, close(connfd)) << "close failed: " << strerror(errno);
}

TEST(NoNetworkTest, NonBlockingConnectNetV6) {
  int connfd = socket(AF_INET6, SOCK_STREAM | SOCK_NONBLOCK, 0);
  ASSERT_GE(connfd, 0) << "socket failed: " << strerror(errno);

  struct sockaddr_in6 addr;
  addr.sin6_family = AF_INET6;
  inet_pton(AF_INET6, "fe80::1", &addr.sin6_addr);  // linklocal address
  addr.sin6_port = htons(10000);

  int ret = connect(connfd, (const struct sockaddr*)&addr, sizeof(addr));
  ASSERT_EQ(-1, ret);
  ASSERT_EQ(ENETUNREACH, errno) << "connect failed: " << strerror(errno);

  ASSERT_EQ(0, close(connfd)) << "close failed: " << strerror(errno);
}

}  // namespace

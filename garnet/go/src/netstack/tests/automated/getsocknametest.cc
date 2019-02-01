// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These tests ensure the zircon libc can talk to netstack.
// No network connection is required, only a running netstack binary.

#include <sys/socket.h>

#include "gtest/gtest.h"

TEST(GetSockNameTest, Localhost) {
  int sockfd = socket(AF_INET6, SOCK_STREAM, 0);
  ASSERT_GE(sockfd, 0) << strerror(errno);

  struct sockaddr sa;
  socklen_t len = sizeof(sa);
  ASSERT_EQ(getsockname(sockfd, &sa, &len), 0) << strerror(errno);
  ASSERT_EQ(sa.sa_family, AF_INET6);
}

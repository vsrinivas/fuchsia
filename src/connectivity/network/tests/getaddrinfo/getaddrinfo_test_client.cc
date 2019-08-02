// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>

#include "gtest/gtest.h"

TEST(GetAddrInfoTest, GetAddrInfoTest) {
  struct addrinfo hints;
  struct addrinfo *result;
  struct sockaddr_in *addr4;
  struct sockaddr_in6 *addr6;
  struct in_addr golden_v4;
  inet_pton(AF_INET, "192.0.2.1", &golden_v4);
  struct in6_addr golden_v6;
  inet_pton(AF_INET6, "2001:db8::1", &golden_v6);

  // Test AF_INET / SOCK_STREAM / http
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  ASSERT_EQ(getaddrinfo("example.com", "http", &hints, &result), 0);
  addr4 = reinterpret_cast<struct sockaddr_in *>(result->ai_addr);
  ASSERT_EQ(((struct in_addr)addr4->sin_addr).s_addr, golden_v4.s_addr);
  ASSERT_EQ(addr4->sin_port, htons(80));

  freeaddrinfo(result);

  // Test AF_INET6 / SOCK_DGRAM / ntp
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET6;
  hints.ai_socktype = SOCK_DGRAM;

  ASSERT_EQ(getaddrinfo("example.com", "ntp", &hints, &result), 0);
  addr6 = reinterpret_cast<struct sockaddr_in6 *>(result->ai_addr);
  ASSERT_EQ(memcmp(golden_v6.s6_addr, ((struct in6_addr)addr6->sin6_addr).s6_addr, 16), 0);
  ASSERT_EQ(addr6->sin6_port, htons(123));

  freeaddrinfo(result);

  // Test EAI_NONAME
  memset(&hints, 0, sizeof(struct addrinfo));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  ASSERT_EQ(getaddrinfo("google.com", "http", &hints, &result), EAI_NONAME);
}

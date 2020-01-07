// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "addr.h"

#include <gtest/gtest.h>

TEST(AddrTest, TestInAddr) {
  InAddr addr;
  EXPECT_FALSE(addr.IsSet());
  EXPECT_EQ(addr.Name(), "<unspec>");
  EXPECT_TRUE(addr.Set("192.168.0.1"));
  EXPECT_TRUE(addr.IsAddr4());
  EXPECT_EQ(addr.Name(), "192.168.0.1");
  union {
    uint8_t b[4];
    uint32_t x;
  } ip4 = {{192, 168, 0, 1}};
  EXPECT_EQ(addr.GetAddr4().s_addr, ip4.x);
  EXPECT_TRUE(addr.Set("FF01:0:0:0:0:0:0:1"));
  EXPECT_TRUE(addr.IsAddr6());
  EXPECT_EQ(addr.Name(), "[FF01:0:0:0:0:0:0:1]");
  const uint8_t cmp[] = {0xFF, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
  EXPECT_EQ(memcmp(addr.GetAddr6().s6_addr, cmp, 16), 0);
  EXPECT_TRUE(addr.Set("null"));
  EXPECT_FALSE(addr.IsSet());
  EXPECT_TRUE(addr.Set("any"));
  EXPECT_TRUE(addr.IsAddr4());
  EXPECT_EQ(addr.GetAddr4().s_addr, 0u);
  EXPECT_TRUE(addr.Set("any6"));
  EXPECT_TRUE(addr.IsAddr6());
  const uint8_t zeros[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                           0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  EXPECT_EQ(memcmp(addr.GetAddr6().s6_addr, zeros, 16), 0);

  EXPECT_FALSE(addr.Set("bad"));
  EXPECT_FALSE(addr.Set("192.168.0."));
  EXPECT_FALSE(addr.Set("FF01::jk"));
}

TEST(AddrTest, LocalIfAddr) {
  LocalIfAddr addr;
  EXPECT_FALSE(addr.IsSet());
  EXPECT_FALSE(addr.HasAddr4());
  EXPECT_FALSE(addr.HasAddr6());
  EXPECT_FALSE(addr.HasId());
  EXPECT_TRUE(addr.Set("192.168.0.1%2"));
  EXPECT_TRUE(addr.HasAddr4());
  EXPECT_TRUE(addr.HasId());
  EXPECT_EQ(addr.Name(), "192.168.0.1%2");
  EXPECT_EQ(addr.GetId(), 2);
  EXPECT_TRUE(addr.Set("192.168.0.1"));
  EXPECT_TRUE(addr.HasAddr4());
  EXPECT_FALSE(addr.HasId());
  EXPECT_EQ(addr.Name(), "192.168.0.1");
  EXPECT_TRUE(addr.Set("FF01:0:0:0:0:0:0:1%3"));
  EXPECT_TRUE(addr.HasAddr6());
  EXPECT_TRUE(addr.HasId());
  EXPECT_EQ(addr.GetId(), 3);
  EXPECT_EQ(addr.Name(), "[FF01:0:0:0:0:0:0:1]%3");
  EXPECT_FALSE(addr.Set("192.168.0.1%abc"));
}

TEST(AddrTest, SockAddrIn) {
  SockAddrIn addr;
  EXPECT_TRUE(addr.Set("192.168.0.1:2020"));
  EXPECT_EQ(addr.Name(), "192.168.0.1:2020");
  struct sockaddr_storage store {};
  auto* sock = reinterpret_cast<struct sockaddr*>(&store);
  int l = sizeof(store);
  EXPECT_TRUE(addr.Fill(sock, &l));
  EXPECT_EQ(sock->sa_family, AF_INET);
  union {
    uint8_t b[4];
    uint32_t x;
  } ip4 = {{192, 168, 0, 1}};
  auto* addr_in = reinterpret_cast<struct sockaddr_in*>(&store);
  EXPECT_EQ(addr_in->sin_addr.s_addr, ip4.x);
  EXPECT_EQ(addr_in->sin_port, htons(2020));
  EXPECT_TRUE(addr.Set("[FF01:0:0:0:0:0:0:1]:4040"));
  EXPECT_EQ(addr.Name(), "[FF01:0:0:0:0:0:0:1]:4040");
  l = sizeof(store);
  EXPECT_TRUE(addr.Fill(sock, &l));
  auto* addr_in6 = reinterpret_cast<struct sockaddr_in6*>(&store);
  const uint8_t cmp[] = {0xFF, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                         0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
  EXPECT_EQ(memcmp(addr_in6->sin6_addr.s6_addr, cmp, 16), 0);
  EXPECT_EQ(addr_in6->sin6_port, htons(4040));
}

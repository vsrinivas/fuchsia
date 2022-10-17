// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "transport.h"

#include <zxtest/zxtest.h>

namespace {

int receive_packet(void*, size_t, void* ctx) { return *static_cast<int*>(ctx); }

int send_packet(const void*, size_t, void* ctx) { return *static_cast<int*>(ctx); }

TEST(TransportTest, ReceivePacket) {
  int callback_ret = 0;
  const size_t packet_size = 1;
  FastbootTCPTransport transport(&callback_ret, packet_size, receive_packet, send_packet);
  uint8_t buf[8];
  zx::result<size_t> ret = transport.ReceivePacket(buf, sizeof(buf));
  ASSERT_TRUE(ret.is_ok());
  ASSERT_EQ(ret.value(), packet_size);
}

TEST(TransportTest, ReceivePacketFailsOnNull) {
  int callback_ret = 0;
  const size_t packet_size = 1;
  FastbootTCPTransport transport(&callback_ret, packet_size, receive_packet, send_packet);
  zx::result<size_t> ret = transport.ReceivePacket(nullptr, 10);
  ASSERT_FALSE(ret.is_ok());
}

TEST(TransportTest, ReceivePacketFailsOnCapacity) {
  int callback_ret = 0;
  const size_t packet_size = 10;
  FastbootTCPTransport transport(&callback_ret, packet_size, receive_packet, send_packet);
  uint8_t buf[8];
  zx::result<size_t> ret = transport.ReceivePacket(buf, sizeof(buf));
  ASSERT_FALSE(ret.is_ok());
}

TEST(TransportTest, ReceivePacketFailsOnCallback) {
  int callback_ret = 1;
  const size_t packet_size = 1;
  FastbootTCPTransport transport(&callback_ret, packet_size, receive_packet, send_packet);
  uint8_t buf[8];
  zx::result<size_t> ret = transport.ReceivePacket(buf, sizeof(buf));
  ASSERT_FALSE(ret.is_ok());
}

TEST(TransportTest, Send) {
  int callback_ret = 0;
  FastbootTCPTransport transport(&callback_ret, 1, receive_packet, send_packet);
  zx::result<> ret = transport.Send("payload");
  ASSERT_TRUE(ret.is_ok());
}

TEST(TransportTest, SendFailsOnCallback) {
  int callback_ret = 1;
  FastbootTCPTransport transport(&callback_ret, 1, receive_packet, send_packet);
  zx::result<> ret = transport.Send("payload");
  ASSERT_FALSE(ret.is_ok());
}

}  // namespace

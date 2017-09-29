// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stop_reply_packet.h"

#include "gtest/gtest.h"

namespace debugserver {
namespace {

void ExpectPacketEquals(const std::vector<char>& packet,
                        const fxl::StringView& expected) {
  EXPECT_EQ(expected, fxl::StringView(packet.data(), packet.size()));
}

TEST(StopReplyPacketTest, ReceivedSignal) {
  StopReplyPacket stop_reply(StopReplyPacket::Type::kReceivedSignal);
  stop_reply.SetSignalNumber(11);

  auto packet = stop_reply.Build();
  ExpectPacketEquals(packet, "S0b");

  stop_reply.SetThreadId(12345, 6789);
  packet = stop_reply.Build();
  ExpectPacketEquals(packet, "T0bthread:p3039.1A85;");

  stop_reply.AddRegisterValue(6, "000102030405060708");
  stop_reply.AddRegisterValue(7, "090A0B0C0D0E0F1011");
  packet = stop_reply.Build();
  ExpectPacketEquals(
      packet,
      "T0b06:000102030405060708;07:090A0B0C0D0E0F1011;thread:p3039.1A85;");

  stop_reply.SetStopReason("swbreak");
  packet = stop_reply.Build();
  ExpectPacketEquals(
      packet,
      "T0506:000102030405060708;07:090A0B0C0D0E0F1011;thread:p3039.1A85;"
      "swbreak:;");
}

}  // namespace
}  // namespace debugserver

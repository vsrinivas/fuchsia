// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/bluetooth/hci/command_packet.h"
#include "apps/bluetooth/hci/event_packet.h"

#include <array>
#include <cstdint>

#include "gtest/gtest.h"

#include "apps/bluetooth/common/byte_buffer.h"
#include "apps/bluetooth/common/test_helpers.h"

using bluetooth::common::ContainersEqual;
using bluetooth::common::StaticByteBuffer;

namespace bluetooth {
namespace hci {
namespace {

constexpr OpCode kTestOpCode = 0x07FF;
constexpr EventCode kTestEventCode = 0xFF;

struct TestPayload {
  uint8_t foo;
};

TEST(HCIPacketTest, CommandPacket) {
  constexpr size_t kPayloadSize = sizeof(TestPayload);
  constexpr size_t kBufferSize = CommandPacket::GetMinBufferSize(kPayloadSize);
  StaticByteBuffer<kBufferSize> buffer;

  CommandPacket packet(kTestOpCode, &buffer, kPayloadSize);

  EXPECT_EQ(kTestOpCode, packet.opcode());
  EXPECT_EQ(kPayloadSize, packet.GetPayloadSize());

  packet.GetPayload<TestPayload>()->foo = 127;
  packet.EncodeHeader();

  constexpr std::array<uint8_t, kBufferSize> kExpected{{
    0xFF, 0x07,  // opcode
    0x01,        // parameter_total_size
    0x7F,        // foo
  }};
  EXPECT_TRUE(ContainersEqual(kExpected, buffer));
}

TEST(HCIPacketTest, EventPacket) {
  constexpr size_t kPayloadSize = sizeof(TestPayload);
  constexpr size_t kBufferSize = EventPacket::GetMinBufferSize(kPayloadSize);
  StaticByteBuffer<kBufferSize> buffer;

  EventPacket packet(kTestEventCode, &buffer, kPayloadSize);

  EXPECT_EQ(kTestEventCode, packet.event_code());
  EXPECT_EQ(kPayloadSize, packet.GetPayloadSize());

  packet.GetPayload<TestPayload>()->foo = 127;
  packet.EncodeHeader();

  constexpr std::array<uint8_t, kBufferSize> kExpected{{
    0xFF,  // event code
    0x01,  // parameter_total_size
    0x7F,  // foo
  }};
  EXPECT_TRUE(ContainersEqual(kExpected, buffer));
}

}  // namespace
}  // namespace hci
}  // namespace bluetooth

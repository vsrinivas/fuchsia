// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/bluetooth/hci/command_packet.h"
#include "apps/bluetooth/hci/event_packet.h"

#include <endian.h>

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

  packet.GetMutablePayload<TestPayload>()->foo = 127;
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
  auto bytes = common::CreateStaticByteBuffer(
      0xFF,  // event code
      0x01,  // parameter_total_size
      0x7F  // foo
  );
  EventPacket packet(&bytes);

  EXPECT_EQ(kTestEventCode, packet.event_code());
  EXPECT_EQ(kPayloadSize, packet.GetPayloadSize());
  EXPECT_EQ(127, packet.GetPayload<TestPayload>()->foo);
}

TEST(HCIPacketTest, EventPacketGetReturnParams) {
  auto correct_size_bad_event_code = common::CreateStaticByteBuffer(
      // Event header
      0xFF, 0x04,  // (event_code is not CommandComplete)

      // CommandCompleteEventParams
      0x01, 0xFF, 0x07,

      // Return parameters
      0x7F
  );
  auto cmd_complete_small_payload = common::CreateStaticByteBuffer(
      // Event header
      0x0E, 0x03,

      // CommandCompleteEventParams
      0x01, 0xFF, 0x07
  );
  auto cmd_complete_valid_bytes = common::CreateStaticByteBuffer(
      // Event header
      0x0E, 0x04,

      // CommandCompleteEventParams
      0x01, 0xFF, 0x07,

      // Return parameters
      0x7F
  );

  // If the event code or the payload size don't match, then GetReturnParams should return nullptr.
  EventPacket invalid0(&correct_size_bad_event_code);
  EXPECT_EQ(nullptr, invalid0.GetReturnParams<TestPayload>());
  EventPacket invalid1(&cmd_complete_small_payload);
  EXPECT_EQ(nullptr, invalid1.GetReturnParams<TestPayload>());

  // Good packets
  EventPacket valid0(&cmd_complete_valid_bytes);
  EXPECT_NE(nullptr, valid0.GetReturnParams<TestPayload>());
  EXPECT_EQ(127, valid0.GetReturnParams<TestPayload>()->foo);
}

}  // namespace
}  // namespace hci
}  // namespace bluetooth

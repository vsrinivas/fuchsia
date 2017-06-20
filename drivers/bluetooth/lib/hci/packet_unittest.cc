// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/bluetooth/lib/hci/acl_data_packet.h"
#include "apps/bluetooth/lib/hci/command_packet.h"
#include "apps/bluetooth/lib/hci/event_packet.h"

#include <endian.h>

#include <array>
#include <cstdint>

#include "gtest/gtest.h"

#include "apps/bluetooth/lib/common/byte_buffer.h"
#include "apps/bluetooth/lib/common/test_helpers.h"

using bluetooth::common::ContainersEqual;
using bluetooth::common::StaticByteBuffer;

namespace bluetooth {
namespace hci {
namespace test {
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
  EXPECT_EQ(kPayloadSize, packet.payload_size());

  packet.mutable_payload<TestPayload>()->foo = 127;
  packet.EncodeHeader();

  constexpr std::array<uint8_t, kBufferSize> kExpected{{
      0xFF, 0x07,  // opcode
      0x01,        // parameter_total_size
      0x7F,        // foo
  }};
  EXPECT_TRUE(ContainersEqual(kExpected, buffer));
}

TEST(HCIPacketTest, CommandPacketFromBuffer) {
  constexpr size_t kPayloadSize = sizeof(TestPayload);
  constexpr size_t kBufferSize = CommandPacket::GetMinBufferSize(kPayloadSize);
  StaticByteBuffer<kBufferSize> buffer;

  CommandPacket packet(kTestOpCode, &buffer, kPayloadSize);

  EXPECT_EQ(kTestOpCode, packet.opcode());
  EXPECT_EQ(kPayloadSize, packet.payload_size());

  packet.EncodeHeader();

  CommandPacket packet0(&buffer);

  EXPECT_EQ(kTestOpCode, packet.opcode());
  EXPECT_EQ(kPayloadSize, packet.payload_size());
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
  EXPECT_EQ(kPayloadSize, packet.payload_size());
  EXPECT_EQ(127, packet.payload<TestPayload>().foo);
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

TEST(HCIPacketTest, GetLEEventParams) {
  auto correct_size_bad_event_code = common::CreateStaticByteBuffer(
    // Event header
    0xFF, 0x02,  // (event_code is not LEMetaEventCode)

    // Subevent code
    0xFF,

    // Subevent payload
    0x7F
  );
  auto payload_too_small = common::CreateStaticByteBuffer(
    0x3E, 0x01,

    // Subevent code
    0xFF
  );
  auto valid = common::CreateStaticByteBuffer(
    // Event header
    0x3E, 0x02,

    // Subevent code
    0xFF,

    // Subevent payload
    0x7F
  );

  // If the event code or the payload size don't match, then GetReturnParams should return nullptr.
  EventPacket invalid0(&correct_size_bad_event_code);
  EXPECT_EQ(nullptr, invalid0.GetLEEventParams<TestPayload>());
  EventPacket invalid1(&payload_too_small);
  EXPECT_EQ(nullptr, invalid1.GetLEEventParams<TestPayload>());

  // Good packets
  EventPacket valid0(&valid);
  EXPECT_NE(nullptr, valid0.GetLEEventParams<TestPayload>());
  EXPECT_EQ(127, valid0.GetLEEventParams<TestPayload>()->foo);
}

TEST(HCIPacketTest, ACLDataTxPacket) {
  constexpr size_t kMaxDataLength = 10;
  constexpr size_t kDataLength = 1;
  constexpr size_t kBufferSize = ACLDataTxPacket::GetMinBufferSize(kMaxDataLength);
  common::StaticByteBuffer<kBufferSize> buffer;
  buffer.SetToZeros();

  ACLDataTxPacket packet(0x007F, ACLPacketBoundaryFlag::kContinuingFragment,
                         ACLBroadcastFlag::kActiveSlaveBroadcast, kDataLength, &buffer);
  packet.EncodeHeader();

  // First 12-bits: 0x07F
  // Upper 4-bits: 0b0101
  EXPECT_TRUE(
      ContainersEqual(packet.data(), std::array<uint8_t, 5>{{0x7F, 0x50, 0x01, 0x00, 0x00}}));

  packet = ACLDataTxPacket(0x0FFF, ACLPacketBoundaryFlag::kCompletePDU,
                           ACLBroadcastFlag::kActiveSlaveBroadcast, kDataLength, &buffer);
  packet.EncodeHeader();

  // First 12-bits: 0xFFF
  // Upper 4-bits: 0b0111
  EXPECT_TRUE(
      ContainersEqual(packet.data(), std::array<uint8_t, 5>{{0xFF, 0x7F, 0x01, 0x00, 0x00}}));

  packet = ACLDataTxPacket(0x0FFF, ACLPacketBoundaryFlag::kFirstNonFlushable,
                           ACLBroadcastFlag::kPointToPoint, kMaxDataLength, &buffer);
  packet.EncodeHeader();

  // First 12-bits: 0xFFF
  // Upper 4-bits: 0b0000
  EXPECT_TRUE(ContainersEqual(
      packet.data(), std::array<uint8_t, 14>{{0xFF, 0x0F, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                              0x00, 0x00, 0x00, 0x00}}));
}

TEST(HCIPacketTest, ACLDataRxPacket) {
  // The same test cases as ACLDataTxPacket test above but in the opposite direction.
  auto bytes = common::CreateStaticByteBuffer(0x7F, 0x50, 0x01, 0x00, 0x00);
  ACLDataRxPacket packet(&bytes);
  EXPECT_EQ(0x007F, packet.GetConnectionHandle());
  EXPECT_EQ(ACLPacketBoundaryFlag::kContinuingFragment, packet.GetPacketBoundaryFlag());
  EXPECT_EQ(ACLBroadcastFlag::kActiveSlaveBroadcast, packet.GetBroadcastFlag());
  EXPECT_EQ(1u, packet.payload_size());

  bytes = common::CreateStaticByteBuffer(0xFF, 0x7F, 0x01, 0x00, 0x00);
  packet = ACLDataRxPacket(&bytes);
  EXPECT_EQ(0x0FFF, packet.GetConnectionHandle());
  EXPECT_EQ(ACLPacketBoundaryFlag::kCompletePDU, packet.GetPacketBoundaryFlag());
  EXPECT_EQ(ACLBroadcastFlag::kActiveSlaveBroadcast, packet.GetBroadcastFlag());
  EXPECT_EQ(1u, packet.payload_size());

  // 256 + 4
  auto large_bytes = common::StaticByteBuffer<260>();
  large_bytes.SetToZeros();
  large_bytes[0] = 0xFF;
  large_bytes[1] = 0x0F;
  large_bytes[2] = 0x00;
  large_bytes[3] = 0x01;
  packet = ACLDataRxPacket(&large_bytes);
  EXPECT_EQ(0x0FFF, packet.GetConnectionHandle());
  EXPECT_EQ(ACLPacketBoundaryFlag::kFirstNonFlushable, packet.GetPacketBoundaryFlag());
  EXPECT_EQ(ACLBroadcastFlag::kPointToPoint, packet.GetBroadcastFlag());
  EXPECT_EQ(256u, packet.payload_size());
}

}  // namespace
}  // namespace test
}  // namespace hci
}  // namespace bluetooth

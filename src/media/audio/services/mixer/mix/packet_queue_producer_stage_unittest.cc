// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/packet_queue_producer_stage.h"

#include <lib/zx/time.h>

#include <map>
#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/packet_view.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"
#include "src/media/audio/services/mixer/mix/testing/test_fence.h"

namespace media_audio {
namespace {

using ::fuchsia_mediastreams::wire::AudioSampleFormat;
using ::testing::ElementsAre;
using ClearCommand = PacketQueueProducerStage::ClearCommand;
using PushPacketCommand = PacketQueueProducerStage::PushPacketCommand;

const Format kFormat = Format::CreateOrDie({AudioSampleFormat::kFloat, 2, 48000});

class PacketQueueProducerStageTest : public ::testing::Test {
 public:
  PacketQueueProducerStageTest()
      : command_queue_(std::make_shared<PacketQueueProducerStage::CommandQueue>()),
        packet_queue_producer_stage_({
            .format = kFormat,
            .reference_clock_koid = DefaultClockKoid(),
            .command_queue = command_queue_,
        }) {}

  const void* SendPushPacketCommand(uint32_t packet_id, int64_t start = 0, int64_t length = 1) {
    auto& packet = NewPacket(packet_id, start, length);
    command_queue_->push(PushPacketCommand{.packet = packet.view, .fence = packet.fence.Take()});
    return packet.payload.data();
  }

  void SendClearCommand(zx::eventpair fence) {
    command_queue_->push(ClearCommand{.fence = std::move(fence)});
  }

  PacketQueueProducerStage::CommandQueue& command_queue() { return *command_queue_; }
  PacketQueueProducerStage& packet_queue_producer_stage() { return packet_queue_producer_stage_; }

  const std::vector<uint32_t>& released_packets() {
    for (auto& [id, packet] : packets_) {
      if (!packet.released && packet.fence.Done()) {
        released_packets_.push_back(id);
        packet.released = true;
      }
    }
    return released_packets_;
  }

 private:
  struct Packet {
    explicit Packet(int64_t start, int64_t length)
        : payload(length, 0.0f),
          view({
              .format = kFormat,
              .start = Fixed(start),
              .length = length,
              .payload = payload.data(),
          }) {}

    std::vector<float> payload;
    PacketView view;
    TestFence fence;
    bool released = false;
  };

  Packet& NewPacket(uint32_t packet_id, int64_t start, int64_t length) {
    auto [it, inserted] = packets_.try_emplace(packet_id, start, length);
    FX_CHECK(inserted) << "duplicate packet with id " << packet_id;
    return it->second;
  }

  std::shared_ptr<PacketQueueProducerStage::CommandQueue> command_queue_;
  PacketQueueProducerStage packet_queue_producer_stage_;
  std::map<int32_t, Packet> packets_;  // ordered map so iteration is deterministic
  std::vector<uint32_t> released_packets_;
};

TEST_F(PacketQueueProducerStageTest, ReadWithCommandQueue) {
  PacketQueueProducerStage& packet_queue = packet_queue_producer_stage();
  EXPECT_TRUE(released_packets().empty());

  // Push some packets onto the command queue.
  const void* payload_0 = SendPushPacketCommand(0, 0, 20);
  const void* payload_1 = SendPushPacketCommand(1, 20, 20);
  SendPushPacketCommand(2, 40, 20);
  EXPECT_TRUE(released_packets().empty());

  // Pop the first two packet.
  {
    // Packet #0:
    const auto buffer = packet_queue.Read(DefaultCtx(), Fixed(0), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(0, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(20, buffer->end());
    EXPECT_EQ(payload_0, buffer->payload());
  }
  EXPECT_THAT(released_packets(), ElementsAre(0));

  {
    // Packet #1:
    const auto buffer = packet_queue.Read(DefaultCtx(), Fixed(20), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(20, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(40, buffer->end());
    EXPECT_EQ(payload_1, buffer->payload());
  }
  EXPECT_THAT(released_packets(), ElementsAre(0, 1));

  // Push payload_3, then a clear command, then payload_4 and payload_5.
  // This should drop everything before payload_4.
  SendPushPacketCommand(3, 60, 20);
  TestFence clear_fence;
  SendClearCommand(clear_fence.Take());
  const void* payload_4 = SendPushPacketCommand(4, 80, 20);
  const void* payload_5 = SendPushPacketCommand(5, 100, 20);

  {
    // Start reading at packet #2 but allow up through packet #4.
    // This should return packet #4.
    const auto buffer = packet_queue.Read(DefaultCtx(), Fixed(40), 60);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(80, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(100, buffer->end());
    EXPECT_EQ(payload_4, buffer->payload());
  }
  EXPECT_THAT(released_packets(), ElementsAre(0, 1, 2, 3, 4));
  EXPECT_TRUE(clear_fence.Done());

  {
    // Packet #5.
    const auto buffer = packet_queue.Read(DefaultCtx(), Fixed(100), 20);
    ASSERT_TRUE(buffer);
    EXPECT_EQ(100, buffer->start());
    EXPECT_EQ(20, buffer->length());
    EXPECT_EQ(120, buffer->end());
    EXPECT_EQ(payload_5, buffer->payload());
  }
}

TEST_F(PacketQueueProducerStageTest, AdvanceWithCommandQueue) {
  PacketQueueProducerStage& packet_queue = packet_queue_producer_stage();
  EXPECT_TRUE(released_packets().empty());

  // Push some packets onto the command queue.
  SendPushPacketCommand(0, 0, 20);
  SendPushPacketCommand(1, 20, 20);
  SendPushPacketCommand(2, 40, 20);

  // The packet queue is still empty because we haven't processed those commands yet.
  EXPECT_TRUE(released_packets().empty());

  // Advancing past the second packet should release the first two packets.
  packet_queue.Advance(DefaultCtx(), Fixed(40));
  EXPECT_THAT(released_packets(), ElementsAre(0, 1));

  // Finally advance past the third packet.
  packet_queue.Advance(DefaultCtx(), Fixed(60));
  EXPECT_THAT(released_packets(), ElementsAre(0, 1, 2));
}

}  // namespace
}  // namespace media_audio

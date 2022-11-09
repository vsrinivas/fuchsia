// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/stream_sink_consumer_writer.h"

#include <lib/zx/time.h>

#include <map>
#include <memory>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/mix/testing/consumer_stage_wrapper.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"

namespace media_audio {
namespace {

using Packet = StreamSinkConsumerWriter::Packet;
using PacketQueue = StreamSinkConsumerWriter::PacketQueue;

using ::fuchsia_audio::SampleType;
using ::fuchsia_audio::wire::Timestamp;
using ::testing::ElementsAre;

const Format kFormat = Format::CreateOrDie({SampleType::kFloat32, 2, 48000});
const int64_t kBytesPerFrame = kFormat.bytes_per_frame();
constexpr int64_t kFramesPerBuffer = 1024;
constexpr int64_t kBufferId = 1;
constexpr int64_t kFramesPerPacket = 10;
const int64_t kBytesPerPacket = kFramesPerPacket * kBytesPerFrame;

struct TestHarness {
  std::unique_ptr<StreamSinkConsumerWriter> writer;
  std::shared_ptr<PacketQueue> recycled_packet_queue;
  std::shared_ptr<MemoryMappedBuffer> buffer;

  // Log of calls to `call_put_packet`.
  std::vector<std::unique_ptr<Packet>> packets;
  std::optional<uint64_t> last_payload_offset;
  // Log of calls to `call_end`. Each value is the `PayloadRange.offset` of the last packet written
  // before the end call, or `std::nullopt` if there were no packets before the End call.
  std::vector<std::optional<uint64_t>> end_calls;
};

// Defaults to one media tick per frame.
TestHarness MakeTestHarness(TimelineRate media_ticks_per_ns = kFormat.frames_per_ns()) {
  TestHarness h;
  h.recycled_packet_queue = std::make_shared<PacketQueue>();
  h.writer = std::make_unique<StreamSinkConsumerWriter>(StreamSinkConsumerWriter::Args{
      .format = kFormat,
      .media_ticks_per_ns = media_ticks_per_ns,
      .call_put_packet =
          [&h](auto packet) mutable {
            fidl::Arena<> arena;
            h.last_payload_offset = packet->ToFidl(arena).payload().offset;
            h.packets.push_back(std::move(packet));
          },
      .call_end = [&h]() mutable { h.end_calls.push_back(h.last_payload_offset); },
      .recycled_packet_queue = h.recycled_packet_queue,
  });

  zx::vmo vmo;
  if (auto status = zx::vmo::create(kFramesPerBuffer * kBytesPerFrame, 0, &vmo); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "zx::vmo::create failed";
  }

  auto buffer_result = MemoryMappedBuffer::Create(std::move(vmo), /*writable=*/true);
  FX_CHECK(buffer_result.is_ok()) << buffer_result.error();
  h.buffer = std::move(buffer_result.value());
  return h;
}

// Constructs a packet that should be recycled before use.
std::unique_ptr<Packet> MakePacket(TestHarness& h, int64_t index) {
  const auto byte_offset = index * kBytesPerPacket;
  const fuchsia_media2::wire::PayloadRange payload_range{
      .buffer_id = kBufferId,
      .offset = static_cast<uint64_t>(byte_offset),
      .size = static_cast<uint64_t>(kFramesPerPacket * kBytesPerFrame),
  };
  return std::make_unique<Packet>(h.buffer, payload_range, h.buffer->offset(byte_offset));
}

// Returns a range of `h.buffer` as a typed payload. Although it would be cheaper to return a
// `cpp20::span` (no copy required), gtest doesn't have a pretty-printer for `cpp20::span`, so
// return a vector to make gtest messages more readable.
std::vector<float> GetPayload(TestHarness& h, int64_t byte_offset, int64_t frame_count) {
  std::vector<float> out(frame_count * kFormat.channels());
  std::memmove(out.data(), h.buffer->offset(byte_offset), frame_count * kBytesPerFrame);
  return out;
}

TEST(StreamSinkConsumerWriterTest, WriteAlignedPackets) {
  auto h = MakeTestHarness();
  h.recycled_packet_queue->push(MakePacket(h, 0));
  h.recycled_packet_queue->push(MakePacket(h, 1));
  h.recycled_packet_queue->push(MakePacket(h, 2));

  const std::vector<float> payload0(kFramesPerPacket * kFormat.channels(), 0.25f);
  const std::vector<float> payload1(kFramesPerPacket * kFormat.channels(), 0.0f);  // silent
  const std::vector<float> payload2(kFramesPerPacket * kFormat.channels(), 0.75f);

  // Call Write once per packet. The write size is aligned with the packet size.
  h.writer->WriteData(100 + 0 * kFramesPerPacket, kFramesPerPacket, payload0.data());
  h.writer->WriteSilence(100 + 1 * kFramesPerPacket, kFramesPerPacket);
  h.writer->WriteData(100 + 2 * kFramesPerPacket, kFramesPerPacket, payload2.data());

  // The first packet should start at timestamp 100 and the following packets should be continuous.
  ASSERT_EQ(h.packets.size(), 3u);
  EXPECT_THAT(h.end_calls, ElementsAre());

  fidl::Arena<> arena;
  auto fidl0 = h.packets[0]->ToFidl(arena);
  auto fidl1 = h.packets[1]->ToFidl(arena);
  auto fidl2 = h.packets[2]->ToFidl(arena);

  EXPECT_EQ(fidl0.payload().buffer_id, kBufferId);
  EXPECT_EQ(fidl0.payload().offset, static_cast<uint64_t>(0 * kBytesPerPacket));
  EXPECT_EQ(fidl0.payload().size, static_cast<uint64_t>(kBytesPerPacket));
  EXPECT_EQ(fidl0.timestamp().Which(), Timestamp::Tag::kSpecified);
  EXPECT_EQ(fidl0.timestamp().specified(), 100);

  EXPECT_EQ(fidl1.payload().buffer_id, kBufferId);
  EXPECT_EQ(fidl1.payload().offset, static_cast<uint64_t>(1 * kBytesPerPacket));
  EXPECT_EQ(fidl1.payload().size, static_cast<uint64_t>(kBytesPerPacket));
  EXPECT_EQ(fidl1.timestamp().Which(), Timestamp::Tag::kUnspecifiedContinuous);

  EXPECT_EQ(fidl2.payload().buffer_id, kBufferId);
  EXPECT_EQ(fidl2.payload().offset, static_cast<uint64_t>(2 * kBytesPerPacket));
  EXPECT_EQ(fidl2.payload().size, static_cast<uint64_t>(kBytesPerPacket));
  EXPECT_EQ(fidl2.timestamp().Which(), Timestamp::Tag::kUnspecifiedContinuous);

  EXPECT_EQ(GetPayload(h, 0 * kBytesPerPacket, kFramesPerPacket), payload0);
  EXPECT_EQ(GetPayload(h, 1 * kBytesPerPacket, kFramesPerPacket), payload1);
  EXPECT_EQ(GetPayload(h, 2 * kBytesPerPacket, kFramesPerPacket), payload2);
}

TEST(StreamSinkConsumerWriterTest, WriteUnalignedPackets) {
  auto h = MakeTestHarness();
  h.recycled_packet_queue->push(MakePacket(h, 0));
  h.recycled_packet_queue->push(MakePacket(h, 1));
  h.recycled_packet_queue->push(MakePacket(h, 2));

  const std::vector<float> nonsilent_payload0(9 * kFormat.channels(), 0.25f);
  const std::vector<float> nonsilent_payload1(19 * kFormat.channels(), 0.75f);

  // These combinations ensure that at least one WriteData and one WriteSilence call span the
  // boundary between two packets.
  h.writer->WriteData(100, 9, nonsilent_payload0.data());
  h.writer->WriteSilence(109, 2);                           // spans boundary of 0->1
  h.writer->WriteData(111, 19, nonsilent_payload1.data());  // spans boundary of 1->2

  // The first packet should start at timestamp 100 and the following packets should be continuous.
  ASSERT_EQ(h.packets.size(), 3u);
  EXPECT_THAT(h.end_calls, ElementsAre());

  fidl::Arena<> arena;
  auto fidl0 = h.packets[0]->ToFidl(arena);
  auto fidl1 = h.packets[1]->ToFidl(arena);
  auto fidl2 = h.packets[2]->ToFidl(arena);

  EXPECT_EQ(fidl0.payload().buffer_id, kBufferId);
  EXPECT_EQ(fidl0.payload().offset, static_cast<uint64_t>(0 * kBytesPerPacket));
  EXPECT_EQ(fidl0.payload().size, static_cast<uint64_t>(kBytesPerPacket));
  EXPECT_EQ(fidl0.timestamp().Which(), Timestamp::Tag::kSpecified);
  EXPECT_EQ(fidl0.timestamp().specified(), 100);

  EXPECT_EQ(fidl1.payload().buffer_id, kBufferId);
  EXPECT_EQ(fidl1.payload().offset, static_cast<uint64_t>(1 * kBytesPerPacket));
  EXPECT_EQ(fidl1.payload().size, static_cast<uint64_t>(kBytesPerPacket));
  EXPECT_EQ(fidl1.timestamp().Which(), Timestamp::Tag::kUnspecifiedContinuous);

  EXPECT_EQ(fidl2.payload().buffer_id, kBufferId);
  EXPECT_EQ(fidl2.payload().offset, static_cast<uint64_t>(2 * kBytesPerPacket));
  EXPECT_EQ(fidl2.payload().size, static_cast<uint64_t>(kBytesPerPacket));
  EXPECT_EQ(fidl2.timestamp().Which(), Timestamp::Tag::kUnspecifiedContinuous);

  std::vector<float> expected_payload0;
  std::vector<float> expected_payload1;
  std::vector<float> expected_payload2;
  expected_payload0.insert(expected_payload0.end(), 9 * kFormat.channels(), 0.25f);
  expected_payload0.insert(expected_payload0.end(), 1 * kFormat.channels(), 0.0f);
  expected_payload1.insert(expected_payload1.end(), 1 * kFormat.channels(), 0.0f);
  expected_payload1.insert(expected_payload1.end(), 9 * kFormat.channels(), 0.75f);
  expected_payload2.insert(expected_payload2.end(), 10 * kFormat.channels(), 0.75f);

  EXPECT_EQ(GetPayload(h, 0 * kBytesPerPacket, kFramesPerPacket), expected_payload0);
  EXPECT_EQ(GetPayload(h, 1 * kBytesPerPacket, kFramesPerPacket), expected_payload1);
  EXPECT_EQ(GetPayload(h, 2 * kBytesPerPacket, kFramesPerPacket), expected_payload2);
}

TEST(StreamSinkConsumerWriterTest, WriteEndWrite) {
  auto h = MakeTestHarness();
  h.recycled_packet_queue->push(MakePacket(h, 0));
  h.recycled_packet_queue->push(MakePacket(h, 1));
  h.recycled_packet_queue->push(MakePacket(h, 2));

  const std::vector<float> payload0(kFramesPerPacket * kFormat.channels(), 0.25f);
  const std::vector<float> payload1(kFramesPerPacket * kFormat.channels(), 0.5f);
  const std::vector<float> payload2(kFramesPerPacket * kFormat.channels(), 0.75f);

  // Write, Write, End, Write, with no gaps.
  h.writer->WriteData(100 + 0 * kFramesPerPacket, kFramesPerPacket, payload0.data());
  h.writer->WriteData(100 + 1 * kFramesPerPacket, kFramesPerPacket, payload1.data());
  h.writer->End();
  h.writer->WriteData(100 + 2 * kFramesPerPacket, kFramesPerPacket, payload2.data());

  // Although packet2 is continuous with packet1, packet2 should have a specified timestamp since it
  // is preceded by an End call. Since the End call occurs after packet1, `h.end_calls` should have
  // one item equal to packet1's byte offset.
  ASSERT_EQ(h.packets.size(), 3u);
  EXPECT_THAT(h.end_calls, ElementsAre(1 * kBytesPerPacket));

  fidl::Arena<> arena;
  auto fidl0 = h.packets[0]->ToFidl(arena);
  auto fidl1 = h.packets[1]->ToFidl(arena);
  auto fidl2 = h.packets[2]->ToFidl(arena);

  EXPECT_EQ(fidl0.payload().buffer_id, kBufferId);
  EXPECT_EQ(fidl0.payload().offset, static_cast<uint64_t>(0 * kBytesPerPacket));
  EXPECT_EQ(fidl0.payload().size, static_cast<uint64_t>(kBytesPerPacket));
  EXPECT_EQ(fidl0.timestamp().Which(), Timestamp::Tag::kSpecified);
  EXPECT_EQ(fidl0.timestamp().specified(), 100);

  EXPECT_EQ(fidl1.payload().buffer_id, kBufferId);
  EXPECT_EQ(fidl1.payload().offset, static_cast<uint64_t>(1 * kBytesPerPacket));
  EXPECT_EQ(fidl1.payload().size, static_cast<uint64_t>(kBytesPerPacket));
  EXPECT_EQ(fidl1.timestamp().Which(), Timestamp::Tag::kUnspecifiedContinuous);

  EXPECT_EQ(fidl2.payload().buffer_id, kBufferId);
  EXPECT_EQ(fidl2.payload().offset, static_cast<uint64_t>(2 * kBytesPerPacket));
  EXPECT_EQ(fidl2.payload().size, static_cast<uint64_t>(kBytesPerPacket));
  EXPECT_EQ(fidl2.timestamp().Which(), Timestamp::Tag::kSpecified);
  EXPECT_EQ(fidl2.timestamp().specified(), 120);

  EXPECT_EQ(GetPayload(h, 0 * kBytesPerPacket, kFramesPerPacket), payload0);
  EXPECT_EQ(GetPayload(h, 1 * kBytesPerPacket, kFramesPerPacket), payload1);
  EXPECT_EQ(GetPayload(h, 2 * kBytesPerPacket, kFramesPerPacket), payload2);
}

TEST(StreamSinkConsumerWriterTest, WriteEndGapWrite) {
  auto h = MakeTestHarness();
  h.recycled_packet_queue->push(MakePacket(h, 0));
  h.recycled_packet_queue->push(MakePacket(h, 1));

  const std::vector<float> payload0(kFramesPerPacket * kFormat.channels(), 0.25f);
  const std::vector<float> payload1(kFramesPerPacket * kFormat.channels(), 0.75f);

  // Write, End, Write, with a gap after End.
  h.writer->WriteData(100, kFramesPerPacket, payload0.data());
  h.writer->End();
  h.writer->WriteData(200, kFramesPerPacket, payload1.data());

  // The packets should have specified timestamps, with a gap.
  ASSERT_EQ(h.packets.size(), 2u);
  EXPECT_THAT(h.end_calls, ElementsAre(0));

  fidl::Arena<> arena;
  auto fidl0 = h.packets[0]->ToFidl(arena);
  auto fidl1 = h.packets[1]->ToFidl(arena);

  EXPECT_EQ(fidl0.payload().buffer_id, kBufferId);
  EXPECT_EQ(fidl0.payload().offset, static_cast<uint64_t>(0 * kBytesPerPacket));
  EXPECT_EQ(fidl0.payload().size, static_cast<uint64_t>(kBytesPerPacket));
  EXPECT_EQ(fidl0.timestamp().Which(), Timestamp::Tag::kSpecified);
  EXPECT_EQ(fidl0.timestamp().specified(), 100);

  EXPECT_EQ(fidl1.payload().buffer_id, kBufferId);
  EXPECT_EQ(fidl1.payload().offset, static_cast<uint64_t>(1 * kBytesPerPacket));
  EXPECT_EQ(fidl1.payload().size, static_cast<uint64_t>(kBytesPerPacket));
  EXPECT_EQ(fidl1.timestamp().Which(), Timestamp::Tag::kSpecified);
  EXPECT_EQ(fidl1.timestamp().specified(), 200);

  EXPECT_EQ(GetPayload(h, 0 * kBytesPerPacket, kFramesPerPacket), payload0);
  EXPECT_EQ(GetPayload(h, 1 * kBytesPerPacket, kFramesPerPacket), payload1);
}

TEST(StreamSinkConsumerWriterTest, PartialWrite) {
  // End before fully writing to the first packet.
  const auto kFramesWritten = kFramesPerPacket - 1;

  auto h = MakeTestHarness();
  h.recycled_packet_queue->push(MakePacket(h, 0));

  const std::vector<float> payload0(kFramesWritten * kFormat.channels(), 0.5f);
  h.writer->WriteData(100, kFramesWritten, payload0.data());
  h.writer->End();

  // The packet should be flushed even through it's not fully written.
  ASSERT_EQ(h.packets.size(), 1u);
  EXPECT_THAT(h.end_calls, ElementsAre(0));

  fidl::Arena<> arena;
  auto fidl0 = h.packets[0]->ToFidl(arena);

  EXPECT_EQ(fidl0.payload().buffer_id, kBufferId);
  EXPECT_EQ(fidl0.payload().offset, static_cast<uint64_t>(0 * kBytesPerPacket));
  EXPECT_EQ(fidl0.payload().size,
            static_cast<uint64_t>(kFramesWritten * kFormat.bytes_per_frame()));
  EXPECT_EQ(fidl0.timestamp().Which(), Timestamp::Tag::kSpecified);
  EXPECT_EQ(fidl0.timestamp().specified(), 100);

  EXPECT_EQ(GetPayload(h, 0 * kBytesPerPacket, kFramesWritten), payload0);
}

TEST(StreamSinkConsumerWriterTest, WriteUnderflowWrite) {
  auto h = MakeTestHarness();
  h.recycled_packet_queue->push(MakePacket(h, 0));
  h.recycled_packet_queue->push(MakePacket(h, 1));

  const std::vector<float> payload0(kFramesPerPacket * kFormat.channels(), 0.25f);
  const std::vector<float> payload1(kFramesPerPacket * kFormat.channels(), 0.75f);

  // Gap (underflow) between the two Writes.
  h.writer->WriteData(100, kFramesPerPacket, payload0.data());
  h.writer->WriteData(200, kFramesPerPacket, payload1.data());

  // The packets should have specified timestamps, with a gap.
  ASSERT_EQ(h.packets.size(), 2u);
  EXPECT_THAT(h.end_calls, ElementsAre());

  fidl::Arena<> arena;
  auto fidl0 = h.packets[0]->ToFidl(arena);
  auto fidl1 = h.packets[1]->ToFidl(arena);

  EXPECT_EQ(fidl0.payload().buffer_id, kBufferId);
  EXPECT_EQ(fidl0.payload().offset, static_cast<uint64_t>(0 * kBytesPerPacket));
  EXPECT_EQ(fidl0.payload().size, static_cast<uint64_t>(kBytesPerPacket));
  EXPECT_EQ(fidl0.timestamp().Which(), Timestamp::Tag::kSpecified);
  EXPECT_EQ(fidl0.timestamp().specified(), 100);

  EXPECT_EQ(fidl1.payload().buffer_id, kBufferId);
  EXPECT_EQ(fidl1.payload().offset, static_cast<uint64_t>(1 * kBytesPerPacket));
  EXPECT_EQ(fidl1.payload().size, static_cast<uint64_t>(kBytesPerPacket));
  EXPECT_EQ(fidl1.timestamp().Which(), Timestamp::Tag::kSpecified);
  EXPECT_EQ(fidl1.timestamp().specified(), 200);

  EXPECT_EQ(GetPayload(h, 0 * kBytesPerPacket, kFramesPerPacket), payload0);
  EXPECT_EQ(GetPayload(h, 1 * kBytesPerPacket, kFramesPerPacket), payload1);
}

TEST(StreamSinkConsumerWriterTest, TranslateToMediaTime) {
  // One media tick every 2 frames.
  auto h = MakeTestHarness(
      /*media_ticks_per_ns=*/TimelineRate(kFormat.frames_per_second() / 2, 1'000'000'000));
  h.recycled_packet_queue->push(MakePacket(h, 0));
  h.writer->WriteSilence(100, kFramesPerPacket);

  // The packet should start at timestamp 50 since there are 2 frames per media tick.
  ASSERT_EQ(h.packets.size(), 1u);
  EXPECT_THAT(h.end_calls, ElementsAre());

  fidl::Arena<> arena;
  auto fidl0 = h.packets[0]->ToFidl(arena);
  EXPECT_EQ(fidl0.timestamp().specified(), 50);
}

}  // namespace
}  // namespace media_audio

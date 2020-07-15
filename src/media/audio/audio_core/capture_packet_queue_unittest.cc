// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/capture_packet_queue.h"

#include <lib/syslog/cpp/macros.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ASF = fuchsia::media::AudioSampleFormat;
using StreamPacket = fuchsia::media::StreamPacket;

namespace media::audio {

namespace {
static constexpr auto kFrameRate = 48000;
static const auto kFormat = Format::Create<ASF::SIGNED_16>(1, kFrameRate).value();
static const auto kBytesPerFrame = kFormat.bytes_per_frame();
}  // namespace

class CapturePacketQueueTest : public ::testing::Test {
 public:
  void SetUp() { CapturePacketQueue::SetMustReleasePackets(true); }

  void CreateMapper(size_t frames) {
    auto status =
        payload_buffer_.CreateAndMap(frames * kBytesPerFrame, /*flags=*/0, nullptr, &payload_vmo_);
    FX_CHECK(status == ZX_OK) << status;
    payload_start_ = static_cast<char*>(payload_buffer_.start());
  }

  using Packet = CapturePacketQueue::Packet;
  void ExpectPacket(fbl::RefPtr<Packet> got, Packet want) {
    EXPECT_EQ(got->stream_packet.payload_buffer_id, want.stream_packet.payload_buffer_id);
    EXPECT_EQ(got->stream_packet.payload_offset, want.stream_packet.payload_offset);
    EXPECT_EQ(got->stream_packet.payload_size, want.stream_packet.payload_size);
    EXPECT_EQ(got->buffer_start, want.buffer_start);
    EXPECT_EQ(got->buffer_end, want.buffer_end);
  }

  zx::vmo payload_vmo_;
  fzl::VmoMapper payload_buffer_;
  char* payload_start_ = nullptr;
};

TEST_F(CapturePacketQueueTest, Preallocated_FramesFitPerfectly) {
  CreateMapper(40);
  auto result = CapturePacketQueue::CreatePreallocated(payload_buffer_, kFormat, 10);
  ASSERT_TRUE(result.is_ok()) << result.error();

  const auto kBytesPerPacket = 10 * kBytesPerFrame;
  auto pq = result.take_value();

  {
    SCOPED_TRACE("front #1");
    ASSERT_FALSE(pq->empty());
    ASSERT_EQ(pq->size(), 4u);
    ExpectPacket(pq->Front(), {
                                  .stream_packet =
                                      {
                                          .payload_buffer_id = 0,
                                          .payload_offset = 0 * kBytesPerPacket,
                                          .payload_size = kBytesPerPacket,
                                      },
                                  .buffer_start = payload_start_ + 0 * kBytesPerPacket,
                                  .buffer_end = payload_start_ + 1 * kBytesPerPacket,
                              });
  }

  {
    SCOPED_TRACE("pop #1");
    ASSERT_FALSE(pq->empty());
    ASSERT_EQ(pq->size(), 4u);
    ExpectPacket(pq->Pop(), {
                                .stream_packet =
                                    {
                                        .payload_buffer_id = 0,
                                        .payload_offset = 0 * kBytesPerPacket,
                                        .payload_size = kBytesPerPacket,
                                    },
                                .buffer_start = payload_start_ + 0 * kBytesPerPacket,
                                .buffer_end = payload_start_ + 1 * kBytesPerPacket,
                            });
  }

  {
    SCOPED_TRACE("pop #2");
    ASSERT_FALSE(pq->empty());
    ASSERT_EQ(pq->size(), 3u);
    ExpectPacket(pq->Pop(), {
                                .stream_packet =
                                    {
                                        .payload_buffer_id = 0,
                                        .payload_offset = 1 * kBytesPerPacket,
                                        .payload_size = kBytesPerPacket,
                                    },
                                .buffer_start = payload_start_ + 1 * kBytesPerPacket,
                                .buffer_end = payload_start_ + 2 * kBytesPerPacket,
                            });
  }

  {
    SCOPED_TRACE("pop #3");
    ASSERT_FALSE(pq->empty());
    ASSERT_EQ(pq->size(), 2u);
    ExpectPacket(pq->Pop(), {
                                .stream_packet =
                                    {
                                        .payload_buffer_id = 0,
                                        .payload_offset = 2 * kBytesPerPacket,
                                        .payload_size = kBytesPerPacket,
                                    },
                                .buffer_start = payload_start_ + 2 * kBytesPerPacket,
                                .buffer_end = payload_start_ + 3 * kBytesPerPacket,
                            });
  }

  {
    SCOPED_TRACE("pop #4");
    ASSERT_FALSE(pq->empty());
    ASSERT_EQ(pq->size(), 1u);
    ExpectPacket(pq->Pop(), {
                                .stream_packet =
                                    {
                                        .payload_buffer_id = 0,
                                        .payload_offset = 3 * kBytesPerPacket,
                                        .payload_size = kBytesPerPacket,
                                    },
                                .buffer_start = payload_start_ + 3 * kBytesPerPacket,
                                .buffer_end = payload_start_ + 4 * kBytesPerPacket,
                            });
  }

  ASSERT_TRUE(pq->empty());
  ASSERT_EQ(pq->size(), 0u);
}

TEST_F(CapturePacketQueueTest, Preallocated_FramesLeftover) {
  CreateMapper(40);
  auto result = CapturePacketQueue::CreatePreallocated(payload_buffer_, kFormat, 15);
  ASSERT_TRUE(result.is_ok()) << result.error();

  const auto kBytesPerPacket = 15 * kBytesPerFrame;
  auto pq = result.take_value();

  {
    SCOPED_TRACE("front #1");
    ASSERT_FALSE(pq->empty());
    ASSERT_EQ(pq->size(), 3u);
    ExpectPacket(pq->Front(), {
                                  .stream_packet =
                                      {
                                          .payload_buffer_id = 0,
                                          .payload_offset = 0 * kBytesPerPacket,
                                          .payload_size = kBytesPerPacket,
                                      },
                                  .buffer_start = payload_start_ + 0 * kBytesPerPacket,
                                  .buffer_end = payload_start_ + 1 * kBytesPerPacket,
                              });
  }

  {
    SCOPED_TRACE("pop #1");
    ASSERT_FALSE(pq->empty());
    ASSERT_EQ(pq->size(), 3u);
    ExpectPacket(pq->Pop(), {
                                .stream_packet =
                                    {
                                        .payload_buffer_id = 0,
                                        .payload_offset = 0 * kBytesPerPacket,
                                        .payload_size = kBytesPerPacket,
                                    },
                                .buffer_start = payload_start_ + 0 * kBytesPerPacket,
                                .buffer_end = payload_start_ + 1 * kBytesPerPacket,
                            });
  }

  {
    SCOPED_TRACE("pop #2");
    ASSERT_FALSE(pq->empty());
    ASSERT_EQ(pq->size(), 2u);
    ExpectPacket(pq->Pop(), {
                                .stream_packet =
                                    {
                                        .payload_buffer_id = 0,
                                        .payload_offset = 1 * kBytesPerPacket,
                                        .payload_size = kBytesPerPacket,
                                    },
                                .buffer_start = payload_start_ + 1 * kBytesPerPacket,
                                .buffer_end = payload_start_ + 2 * kBytesPerPacket,
                            });
  }

  {
    SCOPED_TRACE("pop #3");
    ASSERT_FALSE(pq->empty());
    ASSERT_EQ(pq->size(), 1u);
    ExpectPacket(pq->Pop(), {
                                .stream_packet =
                                    {
                                        .payload_buffer_id = 0,
                                        .payload_offset = 2 * kBytesPerPacket,
                                        .payload_size = kBytesPerPacket,
                                    },
                                .buffer_start = payload_start_ + 2 * kBytesPerPacket,
                                .buffer_end = payload_start_ + 3 * kBytesPerPacket,
                            });
  }

  ASSERT_TRUE(pq->empty());
  ASSERT_EQ(pq->size(), 0u);
}

TEST_F(CapturePacketQueueTest, Preallocated_PopAndRelease_MustRelease) {
  CreateMapper(20);
  auto result = CapturePacketQueue::CreatePreallocated(payload_buffer_, kFormat, 10);
  ASSERT_TRUE(result.is_ok()) << result.error();

  const auto kBytesPerPacket = 10 * kBytesPerFrame;
  auto pq = result.take_value();

  auto p1 = pq->Pop();
  ASSERT_FALSE(pq->empty());
  ASSERT_EQ(pq->size(), 1u);

  {
    SCOPED_TRACE("pop #1");
    ExpectPacket(p1, {
                         .stream_packet =
                             {
                                 .payload_buffer_id = 0,
                                 .payload_offset = 0 * kBytesPerPacket,
                                 .payload_size = kBytesPerPacket,
                             },
                         .buffer_start = payload_start_ + 0 * kBytesPerPacket,
                         .buffer_end = payload_start_ + 1 * kBytesPerPacket,
                     });
  }

  {
    SCOPED_TRACE("pop #2");
    ExpectPacket(pq->Pop(), {
                                .stream_packet =
                                    {
                                        .payload_buffer_id = 0,
                                        .payload_offset = 1 * kBytesPerPacket,
                                        .payload_size = kBytesPerPacket,
                                    },
                                .buffer_start = payload_start_ + 1 * kBytesPerPacket,
                                .buffer_end = payload_start_ + 2 * kBytesPerPacket,
                            });
  }

  ASSERT_TRUE(pq->empty());
  ASSERT_EQ(pq->size(), 0u);

  auto release_result = pq->Release(p1->stream_packet);
  ASSERT_TRUE(release_result.is_ok()) << release_result.error();
  p1 = nullptr;

  // Should pop packet 1 again.
  {
    SCOPED_TRACE("pop #1 again");
    ExpectPacket(pq->Pop(), {
                                .stream_packet =
                                    {
                                        .payload_buffer_id = 0,
                                        .payload_offset = 0 * kBytesPerPacket,
                                        .payload_size = kBytesPerPacket,
                                    },
                                .buffer_start = payload_start_ + 0 * kBytesPerPacket,
                                .buffer_end = payload_start_ + 1 * kBytesPerPacket,
                            });
  }
}

TEST_F(CapturePacketQueueTest, Preallocated_PopAndRelease_DisableRelease) {
  CapturePacketQueue::SetMustReleasePackets(false);

  CreateMapper(20);
  auto result = CapturePacketQueue::CreatePreallocated(payload_buffer_, kFormat, 10);
  ASSERT_TRUE(result.is_ok()) << result.error();

  const auto kBytesPerPacket = 10 * kBytesPerFrame;
  auto pq = result.take_value();
  ASSERT_FALSE(pq->empty());
  ASSERT_EQ(pq->size(), 2u);

  auto p1 = pq->Pop();
  ASSERT_FALSE(pq->empty());
  ASSERT_EQ(pq->size(), 2u);

  {
    SCOPED_TRACE("pop #1");
    ExpectPacket(p1, {
                         .stream_packet =
                             {
                                 .payload_buffer_id = 0,
                                 .payload_offset = 0 * kBytesPerPacket,
                                 .payload_size = kBytesPerPacket,
                             },
                         .buffer_start = payload_start_ + 0 * kBytesPerPacket,
                         .buffer_end = payload_start_ + 1 * kBytesPerPacket,
                     });
  }

  {
    SCOPED_TRACE("pop #2");
    ExpectPacket(pq->Pop(), {
                                .stream_packet =
                                    {
                                        .payload_buffer_id = 0,
                                        .payload_offset = 1 * kBytesPerPacket,
                                        .payload_size = kBytesPerPacket,
                                    },
                                .buffer_start = payload_start_ + 1 * kBytesPerPacket,
                                .buffer_end = payload_start_ + 2 * kBytesPerPacket,
                            });
  }

  ASSERT_FALSE(pq->empty());
  ASSERT_EQ(pq->size(), 2u);

  // Each packet should be released immediately, so another pop
  // should return packet p1 again.
  {
    SCOPED_TRACE("pop #1 again");
    ExpectPacket(pq->Pop(), {
                                .stream_packet =
                                    {
                                        .payload_buffer_id = 0,
                                        .payload_offset = 0,
                                        .payload_size = kBytesPerPacket,
                                    },
                                .buffer_start = payload_start_,
                                .buffer_end = payload_start_ + kBytesPerPacket,
                            });
  }
}

TEST_F(CapturePacketQueueTest, Preallocated_ReleaseErrors) {
  CreateMapper(20);
  auto result = CapturePacketQueue::CreatePreallocated(payload_buffer_, kFormat, 10);
  ASSERT_TRUE(result.is_ok()) << result.error();

  const auto kBytesPerPacket = 10 * kBytesPerFrame;
  auto pq = result.take_value();
  auto p1 = pq->Pop();

  // Offset not found.
  auto release_result = pq->Release({
      .payload_buffer_id = 0,
      .payload_offset = 100,
      .payload_size = kBytesPerPacket,
  });
  ASSERT_TRUE(release_result.is_error());

  // Wrong buffer ID.
  release_result = pq->Release({
      .payload_buffer_id = 1,
      .payload_offset = 0,
      .payload_size = kBytesPerPacket,
  });
  ASSERT_TRUE(release_result.is_error());

  // Wrong size.
  release_result = pq->Release({
      .payload_buffer_id = 0,
      .payload_offset = 0,
      .payload_size = kBytesPerPacket - 1,
  });
  ASSERT_TRUE(release_result.is_error());

  // Double release fails.
  release_result = pq->Release(p1->stream_packet);
  ASSERT_TRUE(release_result.is_ok()) << release_result.error();

  release_result = pq->Release(p1->stream_packet);
  ASSERT_TRUE(release_result.is_error());
}

TEST_F(CapturePacketQueueTest, PopAll) {
  CreateMapper(30);
  auto result = CapturePacketQueue::CreatePreallocated(payload_buffer_, kFormat, 10);
  ASSERT_TRUE(result.is_ok()) << result.error();

  const auto kBytesPerPacket = 10 * kBytesPerFrame;
  auto pq = result.take_value();
  auto packets = pq->PopAll();

  ASSERT_TRUE(pq->empty());
  ASSERT_EQ(pq->size(), 0u);
  ASSERT_EQ(packets.size(), 3u);

  {
    SCOPED_TRACE("packet 1");
    ExpectPacket(packets[0], {
                                 .stream_packet =
                                     {
                                         .payload_buffer_id = 0,
                                         .payload_offset = 0 * kBytesPerPacket,
                                         .payload_size = kBytesPerPacket,
                                     },
                                 .buffer_start = payload_start_ + 0 * kBytesPerPacket,
                                 .buffer_end = payload_start_ + 1 * kBytesPerPacket,
                             });
  }

  {
    SCOPED_TRACE("packet 2");
    ExpectPacket(packets[1], {
                                 .stream_packet =
                                     {
                                         .payload_buffer_id = 0,
                                         .payload_offset = 1 * kBytesPerPacket,
                                         .payload_size = kBytesPerPacket,
                                     },
                                 .buffer_start = payload_start_ + 1 * kBytesPerPacket,
                                 .buffer_end = payload_start_ + 2 * kBytesPerPacket,
                             });
  }

  {
    SCOPED_TRACE("packet 3");
    ExpectPacket(packets[2], {
                                 .stream_packet =
                                     {
                                         .payload_buffer_id = 0,
                                         .payload_offset = 2 * kBytesPerPacket,
                                         .payload_size = kBytesPerPacket,
                                     },
                                 .buffer_start = payload_start_ + 2 * kBytesPerPacket,
                                 .buffer_end = payload_start_ + 3 * kBytesPerPacket,
                             });
  }
}

TEST_F(CapturePacketQueueTest, DynamicallyAllocated) {
  CreateMapper(50);
  auto result = CapturePacketQueue::CreateDynamicallyAllocated(payload_buffer_, kFormat);
  ASSERT_TRUE(result.is_ok()) << result.error();

  auto pq = result.take_value();
  ASSERT_TRUE(pq->empty());
  ASSERT_EQ(pq->size(), 0u);

  bool got_p1_callback = false;
  auto push_result =
      pq->Push(0, 10, [&got_p1_callback](StreamPacket p) { got_p1_callback = true; });
  ASSERT_TRUE(push_result.is_ok()) << push_result.error();
  ASSERT_FALSE(pq->empty());
  ASSERT_EQ(pq->size(), 1u);

  bool got_p2_callback = false;
  push_result = pq->Push(15, 20, [&got_p2_callback](StreamPacket p) { got_p2_callback = true; });
  ASSERT_TRUE(push_result.is_ok()) << push_result.error();
  ASSERT_FALSE(pq->empty());
  ASSERT_EQ(pq->size(), 2u);

  {
    SCOPED_TRACE("pop #1");
    auto p = pq->Pop();
    ExpectPacket(p, {
                        .stream_packet =
                            {
                                .payload_buffer_id = 0,
                                .payload_offset = 0,
                                .payload_size = 10 * kBytesPerFrame,
                            },
                        .buffer_start = payload_start_ + 0 * kBytesPerFrame,
                        .buffer_end = payload_start_ + 10 * kBytesPerFrame,
                    });
    p->callback(p->stream_packet);
    EXPECT_TRUE(got_p1_callback);
    ASSERT_FALSE(pq->empty());
    ASSERT_EQ(pq->size(), 1u);
  }

  {
    SCOPED_TRACE("pop #2");
    auto p = pq->Pop();
    ExpectPacket(p, {
                        .stream_packet =
                            {
                                .payload_buffer_id = 0,
                                .payload_offset = 15 * kBytesPerFrame,
                                .payload_size = 20 * kBytesPerFrame,
                            },
                        .buffer_start = payload_start_ + 15 * kBytesPerFrame,
                        .buffer_end = payload_start_ + 35 * kBytesPerFrame,
                    });
    p->callback(p->stream_packet);
    EXPECT_TRUE(got_p2_callback);
    ASSERT_TRUE(pq->empty());
    ASSERT_EQ(pq->size(), 0u);
  }
}

TEST_F(CapturePacketQueueTest, DynamicallyAllocated_PushErrors) {
  CreateMapper(50);
  auto result = CapturePacketQueue::CreateDynamicallyAllocated(payload_buffer_, kFormat);
  ASSERT_TRUE(result.is_ok()) << result.error();

  auto pq = result.take_value();

  // num_frames == 0
  auto push_result = pq->Push(0, 0, nullptr);
  ASSERT_TRUE(push_result.is_error());

  // Payload goes past end of buffer.
  push_result = pq->Push(40, 11, nullptr);
  ASSERT_TRUE(push_result.is_error());
}

}  // namespace media::audio

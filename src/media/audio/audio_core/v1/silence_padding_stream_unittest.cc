// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/silence_padding_stream.h"

#include <deque>

#include <ffl/string.h>
#include <gtest/gtest.h>

#include "src/media/audio/audio_core/v1/mixer/intersect.h"
#include "src/media/audio/audio_core/v1/testing/fake_audio_core_clock_factory.h"
#include "src/media/audio/audio_core/v1/testing/packet_factory.h"

using ASF = fuchsia::media::AudioSampleFormat;

namespace media::audio {

namespace {

// Used when the ReadLockContext is unused by the test.
static media::audio::ReadableStream::ReadLockContext rlctx;

class FakeStream : public ReadableStream {
 public:
  struct QueuedBuffer {
    Fixed start;
    Fixed end;
    void* payload;
    StreamUsageMask usage;
    float gain;
  };

  explicit FakeStream(std::vector<QueuedBuffer>&& buffers)
      : ReadableStream("FakeStream", Format::Create<ASF::SIGNED_16>(1, 48000).take_value()),
        buffers_(std::move(buffers)),
        audio_clock_(::media::audio::testing::FakeAudioCoreClockFactory::DefaultClock()) {}

  TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override { return {}; }
  std::shared_ptr<Clock> reference_clock() override { return audio_clock_; }

  std::optional<Buffer> ReadLockImpl(ReadLockContext& ctx, Fixed dest_frame,
                                     int64_t frame_count) override {
    if (buffers_.empty()) {
      return std::nullopt;
    }

    auto& next_buffer = buffers_.front();
    Fixed length = next_buffer.end - next_buffer.start;
    FX_CHECK(length.Fraction() == Fixed(0));

    auto packet = mixer::Packet{
        .start = next_buffer.start,
        .length = length.Floor(),
        .payload = next_buffer.payload,
    };
    auto isect = IntersectPacket(format(), packet, dest_frame, frame_count);
    if (!isect) {
      return std::nullopt;
    }

    // When caching, the start dest_frame must intersect the request, but we can cache
    // an arbitrary number of frames in the future. See comments for MakeCachedBuffer.
    Fixed packet_end = packet.start + Fixed(packet.length);
    isect->length = Fixed(packet_end - isect->start).Floor();
    return MakeCachedBuffer(isect->start, isect->length, isect->payload, next_buffer.usage,
                            next_buffer.gain);
  }

  void TrimImpl(Fixed dest_frame) override {
    // Free old buffers.
    while (!buffers_.empty() && buffers_[0].end <= dest_frame) {
      buffers_.erase(buffers_.begin());
    }
  }

 private:
  std::vector<QueuedBuffer> buffers_;
  std::shared_ptr<Clock> audio_clock_;
};

void ExpectNullBuffer(const std::optional<ReadableStream::Buffer>& buffer) {
  EXPECT_FALSE(buffer) << ffl::String::DecRational << "start=" << buffer->start()
                       << " end=" << buffer->end();
}

void ExpectBuffer(const std::optional<ReadableStream::Buffer>& buffer, Fixed want_start,
                  Fixed want_end, int16_t want_sample, StreamUsageMask want_usage,
                  float want_gain) {
  ASSERT_TRUE(buffer);
  EXPECT_EQ(want_sample, reinterpret_cast<int16_t*>(buffer->payload())[0]);
  EXPECT_EQ(want_start, buffer->start());
  EXPECT_EQ(want_end, buffer->end());
  EXPECT_EQ(want_usage, buffer->usage_mask());
  EXPECT_EQ(want_gain, buffer->total_applied_gain_db());
}

}  // namespace

TEST(SilencePaddingStreamTest, EmptySource) {
  auto source = std::make_shared<FakeStream>(std::vector<FakeStream::QueuedBuffer>());
  auto stream =
      SilencePaddingStream::Create(source, Fixed(10), /*fractional_gaps_round_down=*/true);

  // Since the source is empty, the stream should be empty.
  auto buffer = stream->ReadLock(rlctx, Fixed(0), 20);
  ExpectNullBuffer(buffer);
}

TEST(SilencePaddingStreamTest, AfterOneBuffer) {
  const int16_t expected_sample = 1;
  const StreamUsageMask expected_usage({StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});
  const float expected_gain = -20;

  std::vector<int16_t> source_payload(20, expected_sample);
  auto source = std::make_shared<FakeStream>(std::vector<FakeStream::QueuedBuffer>{
      {
          .start = Fixed(0),
          .end = Fixed(20),
          .payload = &source_payload[0],
          .usage = expected_usage,
          .gain = expected_gain,
      },
  });
  auto stream = SilencePaddingStream::Create(source, Fixed(5), /*fractional_gaps_round_down=*/true);

  // Source buffer.
  {
    SCOPED_TRACE("ReadLock(0, 20)");
    auto buffer = stream->ReadLock(rlctx, Fixed(0), 20);
    ExpectBuffer(buffer, Fixed(0), Fixed(20), expected_sample, expected_usage, expected_gain);
  }

  // Silence.
  {
    SCOPED_TRACE("ReadLock(20, 10)");
    auto buffer = stream->ReadLock(rlctx, Fixed(20), 10);
    ExpectBuffer(buffer, Fixed(20), Fixed(25), 0, expected_usage, expected_gain);
  }

  // No further data.
  {
    SCOPED_TRACE("ReadLock(25, 10)");
    auto buffer = stream->ReadLock(rlctx, Fixed(25), 10);
    ExpectNullBuffer(buffer);
  }
}

TEST(SilencePaddingStreamTest, AfterTwoBuffers) {
  const int16_t expected_sample_1 = 1;
  const StreamUsageMask expected_usage_1({StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});
  const float expected_gain_1 = -10;
  std::vector<int16_t> source_payload_1(20, expected_sample_1);

  const int16_t expected_sample_2 = 2;
  const StreamUsageMask expected_usage_2({StreamUsage::WithRenderUsage(RenderUsage::BACKGROUND)});
  const float expected_gain_2 = -20;
  std::vector<int16_t> source_payload_2(20, expected_sample_2);

  auto source = std::make_shared<FakeStream>(std::vector<FakeStream::QueuedBuffer>{
      {
          .start = Fixed(00),
          .end = Fixed(20),
          .payload = &source_payload_1[0],
          .usage = expected_usage_1,
          .gain = expected_gain_1,
      },
      {
          .start = Fixed(20),
          .end = Fixed(40),
          .payload = &source_payload_2[0],
          .usage = expected_usage_2,
          .gain = expected_gain_2,
      },
  });
  auto stream = SilencePaddingStream::Create(source, Fixed(5), /*fractional_gaps_round_down=*/true);

  // First source buffer.
  {
    SCOPED_TRACE("ReadLock(0, 20)");
    auto buffer = stream->ReadLock(rlctx, Fixed(0), 20);
    ExpectBuffer(buffer, Fixed(0), Fixed(20), expected_sample_1, expected_usage_1, expected_gain_1);
  }

  // Second source buffer.
  {
    SCOPED_TRACE("ReadLock(20, 20)");
    auto buffer = stream->ReadLock(rlctx, Fixed(20), 20);
    ExpectBuffer(buffer, Fixed(20), Fixed(40), expected_sample_2, expected_usage_2,
                 expected_gain_2);
  }

  // Silence.
  {
    SCOPED_TRACE("ReadLock(40, 10)");
    auto buffer = stream->ReadLock(rlctx, Fixed(40), 10);
    ExpectBuffer(buffer, Fixed(40), Fixed(45), 0, expected_usage_2, expected_gain_2);
  }

  // No further data.
  {
    SCOPED_TRACE("ReadLock(45, 10)");
    auto buffer = stream->ReadLock(rlctx, Fixed(45), 10);
    ExpectNullBuffer(buffer);
  }
}

TEST(SilencePaddingStreamTest, SkipBuffer) {
  const int16_t expected_sample = 1;
  const StreamUsageMask expected_usage({StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});
  const float expected_gain = -20;

  std::vector<int16_t> source_payload(20, expected_sample);
  auto source = std::make_shared<FakeStream>(std::vector<FakeStream::QueuedBuffer>{
      {
          .start = Fixed(0),
          .end = Fixed(20),
          .payload = &source_payload[0],
          .usage = expected_usage,
          .gain = expected_gain,
      },
  });
  auto stream = SilencePaddingStream::Create(source, Fixed(5), /*fractional_gaps_round_down=*/true);

  // If we completely skip over the source buffer, this is a discontinuity.
  // There's no need to emit silence because there was no prior audio to "ring out".
  {
    SCOPED_TRACE("ReadLock(20, 10)");
    auto buffer = stream->ReadLock(rlctx, Fixed(20), 10);
    ExpectNullBuffer(buffer);
  }
}

TEST(SilencePaddingStreamTest, GapBetweenBuffersLongerThanSilence) {
  const int16_t expected_sample = 1;
  const StreamUsageMask expected_usage({StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});
  const float expected_gain = -20;

  std::vector<int16_t> source_payload(20, expected_sample);
  auto source = std::make_shared<FakeStream>(std::vector<FakeStream::QueuedBuffer>{
      {
          .start = Fixed(10),
          .end = Fixed(20),
          .payload = &source_payload[0],
          .usage = expected_usage,
          .gain = expected_gain,
      },
      {
          .start = Fixed(45),
          .end = Fixed(55),
          .payload = &source_payload[0],
          .usage = expected_usage,
          .gain = expected_gain,
      },
  });
  auto stream = SilencePaddingStream::Create(source, Fixed(5), /*fractional_gaps_round_down=*/true);

  // First source buffer.
  {
    SCOPED_TRACE("ReadLock(0, 20)");
    auto buffer = stream->ReadLock(rlctx, Fixed(0), 20);
    ExpectBuffer(buffer, Fixed(10), Fixed(20), expected_sample, expected_usage, expected_gain);
  }

  // First silence.
  {
    SCOPED_TRACE("ReadLock(20, 10)");
    auto buffer = stream->ReadLock(rlctx, Fixed(20), 10);
    ExpectBuffer(buffer, Fixed(20), Fixed(25), 0, expected_usage, expected_gain);
  }

  // Empty gap.
  {
    SCOPED_TRACE("ReadLock(25, 20)");
    auto buffer = stream->ReadLock(rlctx, Fixed(25), 20);
    ExpectNullBuffer(buffer);
  }

  // Second source buffer.
  {
    SCOPED_TRACE("ReadLock(45, 10)");
    auto buffer = stream->ReadLock(rlctx, Fixed(45), 10);
    ExpectBuffer(buffer, Fixed(45), Fixed(55), expected_sample, expected_usage, expected_gain);
  }

  // Second silence.
  {
    SCOPED_TRACE("ReadLock(55, 10)");
    auto buffer = stream->ReadLock(rlctx, Fixed(55), 10);
    ExpectBuffer(buffer, Fixed(55), Fixed(60), 0, expected_usage, expected_gain);
  }
}

TEST(SilencePaddingStreamTest, GapBetweenBuffersShorterThanSilence) {
  const int16_t expected_sample = 1;
  const StreamUsageMask expected_usage({StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});
  const float expected_gain = -20;

  std::vector<int16_t> source_payload(20, expected_sample);
  auto source = std::make_shared<FakeStream>(std::vector<FakeStream::QueuedBuffer>{
      {
          .start = Fixed(10),
          .end = Fixed(20),
          .payload = &source_payload[0],
          .usage = expected_usage,
          .gain = expected_gain,
      },
      {
          .start = Fixed(21),
          .end = Fixed(31),
          .payload = &source_payload[0],
          .usage = expected_usage,
          .gain = expected_gain,
      },
  });
  auto stream = SilencePaddingStream::Create(source, Fixed(5), /*fractional_gaps_round_down=*/true);

  // First source buffer.
  {
    SCOPED_TRACE("ReadLock(0, 20)");
    auto buffer = stream->ReadLock(rlctx, Fixed(0), 20);
    ExpectBuffer(buffer, Fixed(10), Fixed(20), expected_sample, expected_usage, expected_gain);
  }

  // First silence.
  // Although we've configured 5 frames of silence, the second buffer starts
  // after just one frame of silence, so we emit just one frame of silence.
  {
    SCOPED_TRACE("ReadLock(20, 20)");
    auto buffer = stream->ReadLock(rlctx, Fixed(20), 20);
    ExpectBuffer(buffer, Fixed(20), Fixed(21), 0, expected_usage, expected_gain);
  }

  // Second source buffer.
  {
    SCOPED_TRACE("ReadLock(21, 20)");
    auto buffer = stream->ReadLock(rlctx, Fixed(21), 20);
    ExpectBuffer(buffer, Fixed(21), Fixed(31), expected_sample, expected_usage, expected_gain);
  }
}

TEST(SilencePaddingStreamTest, GapBetweenBuffersShorterThanSilenceAndFractionalRoundDown) {
  const int16_t expected_sample = 1;
  const StreamUsageMask expected_usage({StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});
  const float expected_gain = -20;

  std::vector<int16_t> source_payload(20, expected_sample);
  auto source = std::make_shared<FakeStream>(std::vector<FakeStream::QueuedBuffer>{
      {
          .start = Fixed(10),
          .end = Fixed(20),
          .payload = &source_payload[0],
          .usage = expected_usage,
          .gain = expected_gain,
      },
      {
          .start = Fixed(21) + ffl::FromRatio(1, 2),
          .end = Fixed(31) + ffl::FromRatio(1, 2),
          .payload = &source_payload[0],
          .usage = expected_usage,
          .gain = expected_gain,
      },
  });
  auto stream = SilencePaddingStream::Create(source, Fixed(5), /*fractional_gaps_round_down=*/true);

  // First source buffer.
  {
    SCOPED_TRACE("ReadLock(0, 20)");
    auto buffer = stream->ReadLock(rlctx, Fixed(0), 20);
    ExpectBuffer(buffer, Fixed(10), Fixed(20), expected_sample, expected_usage, expected_gain);
  }

  // First silence.
  // Although we've configured 5 frames of silence, the second buffer starts
  // after just 1.5 frames of silence, so we round down to 1.0 frames of silence.
  {
    SCOPED_TRACE("ReadLock(20, 20)");
    auto buffer = stream->ReadLock(rlctx, Fixed(20), 20);
    ExpectBuffer(buffer, Fixed(20), Fixed(21), 0, expected_usage, expected_gain);
  }

  // Second source buffer.
  {
    SCOPED_TRACE("ReadLock(21, 20)");
    auto buffer = stream->ReadLock(rlctx, Fixed(21), 20);
    ExpectBuffer(buffer, Fixed(21) + ffl::FromRatio(1, 2), Fixed(31) + ffl::FromRatio(1, 2),
                 expected_sample, expected_usage, expected_gain);
  }
}

TEST(SilencePaddingStreamTest, GapBetweenBuffersShorterThanSilenceAndFractionalRoundUp) {
  const int16_t expected_sample = 1;
  const StreamUsageMask expected_usage({StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});
  const float expected_gain = -20;

  std::vector<int16_t> source_payload(20, expected_sample);
  auto source = std::make_shared<FakeStream>(std::vector<FakeStream::QueuedBuffer>{
      {
          .start = Fixed(10),
          .end = Fixed(20),
          .payload = &source_payload[0],
          .usage = expected_usage,
          .gain = expected_gain,
      },
      {
          .start = Fixed(21) + ffl::FromRatio(1, 2),
          .end = Fixed(31) + ffl::FromRatio(1, 2),
          .payload = &source_payload[0],
          .usage = expected_usage,
          .gain = expected_gain,
      },
  });
  auto stream =
      SilencePaddingStream::Create(source, Fixed(5), /*fractional_gaps_round_down=*/false);

  // First source buffer.
  {
    SCOPED_TRACE("ReadLock(0, 20)");
    auto buffer = stream->ReadLock(rlctx, Fixed(0), 20);
    ExpectBuffer(buffer, Fixed(10), Fixed(20), expected_sample, expected_usage, expected_gain);
  }

  // First silence.
  // Although we've configured 5 frames of silence, the second buffer starts
  // after just 1.5 frames of silence, so we round up to 2.0 frames of silence.
  {
    SCOPED_TRACE("ReadLock(20, 20)");
    auto buffer = stream->ReadLock(rlctx, Fixed(20), 20);
    ExpectBuffer(buffer, Fixed(20), Fixed(22), 0, expected_usage, expected_gain);
  }

  // Second source buffer.
  {
    SCOPED_TRACE("ReadLock(22, 20)");
    auto buffer = stream->ReadLock(rlctx, Fixed(22), 20);
    ExpectBuffer(buffer, Fixed(21) + ffl::FromRatio(1, 2), Fixed(31) + ffl::FromRatio(1, 2),
                 expected_sample, expected_usage, expected_gain);
  }
}

TEST(SilencePaddingStreamTest, GapBetweenBuffersLessThanOneFrameRoundDown) {
  const int16_t expected_sample = 1;
  const StreamUsageMask expected_usage({StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});
  const float expected_gain = -20;

  std::vector<int16_t> source_payload(20, expected_sample);
  auto source = std::make_shared<FakeStream>(std::vector<FakeStream::QueuedBuffer>{
      {
          .start = Fixed(10),
          .end = Fixed(20),
          .payload = &source_payload[0],
          .usage = expected_usage,
          .gain = expected_gain,
      },
      {
          .start = Fixed(20) + ffl::FromRatio(1, 2),
          .end = Fixed(30) + ffl::FromRatio(1, 2),
          .payload = &source_payload[0],
          .usage = expected_usage,
          .gain = expected_gain,
      },
  });
  auto stream = SilencePaddingStream::Create(source, Fixed(5), /*fractional_gaps_round_down=*/true);

  // First source buffer.
  {
    SCOPED_TRACE("ReadLock(0, 20)");
    auto buffer = stream->ReadLock(rlctx, Fixed(0), 20);
    ExpectBuffer(buffer, Fixed(10), Fixed(20), expected_sample, expected_usage, expected_gain);
  }

  // Second source buffer.
  // The gap between packets is smaller than one frame and we're rounding down,
  // so don't emit silence.
  {
    SCOPED_TRACE("ReadLock(20, 20)");
    auto buffer = stream->ReadLock(rlctx, Fixed(20), 20);
    ExpectBuffer(buffer, Fixed(20) + ffl::FromRatio(5, 10), Fixed(30) + ffl::FromRatio(5, 10),
                 expected_sample, expected_usage, expected_gain);
  }
}

TEST(SilencePaddingStreamTest, GapBetweenBuffersLessThanOneFrameRoundUp) {
  const int16_t expected_sample = 1;
  const StreamUsageMask expected_usage({StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});
  const float expected_gain = -20;

  std::vector<int16_t> source_payload(20, expected_sample);
  auto source = std::make_shared<FakeStream>(std::vector<FakeStream::QueuedBuffer>{
      {
          .start = Fixed(10),
          .end = Fixed(20),
          .payload = &source_payload[0],
          .usage = expected_usage,
          .gain = expected_gain,
      },
      {
          .start = Fixed(20) + ffl::FromRatio(1, 2),
          .end = Fixed(30) + ffl::FromRatio(1, 2),
          .payload = &source_payload[0],
          .usage = expected_usage,
          .gain = expected_gain,
      },
  });
  auto stream =
      SilencePaddingStream::Create(source, Fixed(5), /*fractional_gaps_round_down=*/false);

  // First source buffer.
  {
    SCOPED_TRACE("ReadLock(0, 20)");
    auto buffer = stream->ReadLock(rlctx, Fixed(0), 20);
    ExpectBuffer(buffer, Fixed(10), Fixed(20), expected_sample, expected_usage, expected_gain);
  }

  // Second source buffer.
  // The gap between packets is smaller than one frame, but we're rounding up,
  // so emit one frame of silence.
  {
    SCOPED_TRACE("ReadLock(20, 20)");
    auto buffer = stream->ReadLock(rlctx, Fixed(20), 20);
    ExpectBuffer(buffer, Fixed(20), Fixed(21), 0, expected_usage, expected_gain);
  }

  // Now read the second source buffer.
  {
    SCOPED_TRACE("ReadLock(21, 20)");
    auto buffer = stream->ReadLock(rlctx, Fixed(21), 20);
    ExpectBuffer(buffer, Fixed(20) + ffl::FromRatio(5, 10), Fixed(30) + ffl::FromRatio(5, 10),
                 expected_sample, expected_usage, expected_gain);
  }
}

TEST(SilencePaddingStreamTest, CreateRoundsUpNumberOfFrames) {
  const int16_t expected_sample = 1;
  const StreamUsageMask expected_usage({StreamUsage::WithRenderUsage(RenderUsage::MEDIA)});
  const float expected_gain = -20;

  std::vector<int16_t> source_payload(20, expected_sample);
  auto source = std::make_shared<FakeStream>(std::vector<FakeStream::QueuedBuffer>{
      {
          .start = Fixed(10),
          .end = Fixed(20),
          .payload = &source_payload[0],
          .usage = expected_usage,
          .gain = expected_gain,
      },
  });
  auto stream = SilencePaddingStream::Create(source, ffl::FromRatio(1, 2),
                                             /*fractional_gaps_round_down=*/true);

  // Source buffer.
  {
    SCOPED_TRACE("ReadLock(0, 20)");
    auto buffer = stream->ReadLock(rlctx, Fixed(0), 20);
    ExpectBuffer(buffer, Fixed(10), Fixed(20), expected_sample, expected_usage, expected_gain);
  }

  // We asked for 0.5 frames of silence, but should get 1.0 frames.
  {
    SCOPED_TRACE("ReadLock(20, 10)");
    auto buffer = stream->ReadLock(rlctx, Fixed(20), 10);
    ExpectBuffer(buffer, Fixed(20), Fixed(21), 0, expected_usage, expected_gain);
  }

  // No more data.
  {
    SCOPED_TRACE("ReadLock(21, 10)");
    auto buffer = stream->ReadLock(rlctx, Fixed(21), 10);
    ExpectNullBuffer(buffer);
  }
}

}  // namespace media::audio

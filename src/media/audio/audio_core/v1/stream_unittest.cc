// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/stream.h"

#include <lib/fit/defer.h>

#include <ffl/string.h>
#include <gtest/gtest.h>

#include "src/media/audio/audio_core/shared/mixer/intersect.h"
#include "src/media/audio/audio_core/v1/testing/fake_audio_core_clock_factory.h"

using ASF = fuchsia::media::AudioSampleFormat;

namespace media::audio {

namespace {

// Used when the ReadLockContext is unused by the test.
media::audio::ReadableStream::ReadLockContext rlctx;

const auto kFormat = Format::Create<ASF::SIGNED_16>(1, 48000).take_value();
const auto kBytesPerFrame = kFormat.bytes_per_frame();

// These tests need to check the value of a Buffer's payload pointer.
// Ideally we might put frame 0 at nullptr, but if we did that, computing the address
// of frame 1 would require adding an offset to nullptr, which is undefined behavior.
// So instead we preallocate a buffer so we have a pointer to actually memory, which
// keeps UBSan happy.
char* const kPayloadBuffer = new char[4096];

// FakeStream acts like a PacketQueue, but can be configured to use either MakeCachedBuffer
// or MakeUncachedBuffer.
class FakeStream : public ReadableStream {
 public:
  struct QueuedBuffer {
    Fixed start;
    Fixed end;
    char* payload;
  };

  FakeStream(bool use_cache, std::vector<QueuedBuffer>&& buffers)
      : ReadableStream("FakeStream", kFormat),
        use_cache_(use_cache),
        buffers_(std::move(buffers)),
        audio_clock_(::media::audio::testing::FakeAudioCoreClockFactory::DefaultClock()),
        timeline_({
            .timeline_function = TimelineFunction(
                0, 0, Fixed(kFormat.frames_per_second()).raw_value(), zx::sec(1).get()),
            .generation = 1,
        }) {}

  TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override {
    return timeline_;
  }
  std::shared_ptr<Clock> reference_clock() override { return audio_clock_; }

  std::optional<Buffer> ReadLockImpl(ReadLockContext& ctx, Fixed frame,
                                     int64_t frame_count) override {
    if (cached_end_ && frame < *cached_end_) {
      ADD_FAILURE() << ffl::String::DecRational << "ReadLock not handled by cache? frame=" << frame
                    << " frame_count=" << frame_count << " cached_end=" << *cached_end_;
    }

    if (buffers_.empty()) {
      return std::nullopt;
    }

    Fixed length = buffers_.front().end - buffers_.front().start;
    FX_CHECK(length.Fraction() == Fixed(0));

    auto packet = mixer::Packet{
        .start = buffers_.front().start,
        .length = length.Floor(),
        .payload = buffers_.front().payload,
    };
    auto isect = IntersectPacket(format(), packet, frame, frame_count);
    if (!isect) {
      return std::nullopt;
    }

    if (use_cache_) {
      // When caching, the start frame must intersect the request, but we can cache
      // an arbitrary number of frames. See comments for MakeCachedBuffer.
      Fixed packet_end = packet.start + Fixed(packet.length);
      isect->length = Fixed(packet_end - isect->start).Floor();
      // When caching, we should not see a ReadLockImpl call that intersects the buffer
      // we are returning (all of those intersections should be handled by the cache).
      cached_end_ = isect->start + Fixed(isect->length);
      return MakeCachedBuffer(isect->start, isect->length, isect->payload, StreamUsageMask(), 0);
    } else {
      return MakeUncachedBuffer(isect->start, isect->length, isect->payload, StreamUsageMask(), 0);
    }
  }

  void TrimImpl(Fixed frame) override {
    if (trim_calls_.empty() || frame > trim_calls_.back()) {
      trim_calls_.push_back(frame);
    }

    // Free old buffers.
    while (!buffers_.empty() && buffers_[0].end <= frame) {
      buffers_.erase(buffers_.begin());
    }
  }

  std::vector<Fixed>& trim_calls() { return trim_calls_; }

  void set_timeline_function(TimelineFunction f) {
    timeline_.timeline_function = f;
    timeline_.generation++;
    cached_end_ = std::nullopt;
  }

  void PushBuffer(QueuedBuffer buffer) { buffers_.push_back(buffer); }

 private:
  const bool use_cache_;
  std::vector<QueuedBuffer> buffers_;
  std::vector<Fixed> trim_calls_;
  std::optional<Fixed> cached_end_;
  std::shared_ptr<Clock> audio_clock_;
  TimelineFunctionSnapshot timeline_;
};

// PassThroughStream is a no-op wrapper around a source stream.
// This is used to test ForwardBuffer.
class PassThroughStream : public ReadableStream {
 public:
  explicit PassThroughStream(std::shared_ptr<FakeStream> src)
      : ReadableStream("PassThroughStream", src->format()), src_(src) {}

  TimelineFunctionSnapshot ref_time_to_frac_presentation_frame() const override {
    return src_->ref_time_to_frac_presentation_frame();
  }
  std::shared_ptr<Clock> reference_clock() override { return src_->reference_clock(); }

  std::optional<Buffer> ReadLockImpl(ReadLockContext& ctx, Fixed frame,
                                     int64_t frame_count) override {
    return ForwardBuffer(src_->ReadLock(ctx, frame, frame_count));
  }

  void TrimImpl(Fixed frame) override { src_->Trim(frame); }

 private:
  std::shared_ptr<FakeStream> src_;
};

// All tests in this file can be run against four pipelines.
enum PipelineType {
  FakeStreamWithCaching,
  FakeStreamWithoutCaching,
  FakeStreamWithCachingThenPassthrough,
  FakeStreamWithoutCachingThenPassthrough,
};

std::string PipelineParamToString(const ::testing::TestParamInfo<PipelineType>& info) {
  switch (info.param) {
    case FakeStreamWithCaching:
      return "FakeStreamWithCaching";
    case FakeStreamWithoutCaching:
      return "FakeStreamWithoutCaching";
    case FakeStreamWithCachingThenPassthrough:
      return "FakeStreamWithCachingThenPassthrough";
    case FakeStreamWithoutCachingThenPassthrough:
      return "FakeStreamWithoutCachingThenPassthrough";
    default:
      FX_CHECK(false) << info.param;
  }
}

class ReadableStreamTest : public ::testing::TestWithParam<PipelineType> {
 protected:
  bool UseCaching() {
    return GetParam() == FakeStreamWithCaching ||
           GetParam() == FakeStreamWithCachingThenPassthrough;
  }

  std::shared_ptr<ReadableStream> MakeStream(std::vector<FakeStream::QueuedBuffer>&& buffers) {
    switch (GetParam()) {
      case FakeStreamWithCaching:
      case FakeStreamWithCachingThenPassthrough:
        fake_stream_ = std::make_shared<FakeStream>(true /* use_cache */, std::move(buffers));
        break;
      case FakeStreamWithoutCaching:
      case FakeStreamWithoutCachingThenPassthrough:
        fake_stream_ = std::make_shared<FakeStream>(false /* use_cache */, std::move(buffers));
        break;
      default:
        FX_CHECK(false) << GetParam();
    }
    switch (GetParam()) {
      case FakeStreamWithCaching:
      case FakeStreamWithoutCaching:
        return fake_stream_;
      case FakeStreamWithCachingThenPassthrough:
      case FakeStreamWithoutCachingThenPassthrough:
        return std::make_shared<PassThroughStream>(fake_stream_);
    }
  }

  void ExpectNullBuffer(const std::optional<ReadableStream::Buffer>& buffer) {
    EXPECT_FALSE(buffer) << "start=" << ffl::String(buffer->start()).c_str()
                         << " end=" << ffl::String(buffer->end()).c_str();
  }

  void ExpectBuffer(const std::optional<ReadableStream::Buffer>& buffer, Fixed want_start,
                    Fixed want_end, char* want_payload) {
    ASSERT_TRUE(buffer);
    EXPECT_EQ(reinterpret_cast<void*>(want_payload), buffer->payload());
    if (want_start != buffer->start()) {
      ADD_FAILURE() << "want_start=" << ffl::String(want_start).c_str()
                    << " start=" << ffl::String(buffer->start()).c_str();
    }
    if (want_end != buffer->end()) {
      ADD_FAILURE() << "want_end=" << ffl::String(want_end).c_str()
                    << " end=" << ffl::String(buffer->end()).c_str();
    }
  }

  // Expect the given sequence of FakeStream::Trim calls since the last call to ExpectTrimCalls.
  void ExpectTrimCalls(const std::vector<Fixed>& want) {
    auto cleanup = fit::defer([this]() { fake_stream_->trim_calls().clear(); });

    if (fake_stream_->trim_calls() == want) {
      return;
    }

    std::ostringstream ostr;
    ostr << "got = {";
    for (auto frame : fake_stream_->trim_calls()) {
      ostr << " " << ffl::String(frame).c_str();
    }
    ostr << "}, want = {";
    for (auto frame : want) {
      ostr << " " << ffl::String(frame).c_str();
    }
    ostr << "}";
    ADD_FAILURE() << ostr.str();
  }

  FakeStream& fake_stream() { return *fake_stream_; }

 private:
  std::shared_ptr<FakeStream> fake_stream_;
};

}  // namespace

TEST_P(ReadableStreamTest, EmptySource) {
  auto stream = MakeStream(std::vector<FakeStream::QueuedBuffer>());
  auto buffer = stream->ReadLock(rlctx, Fixed(0), 20);
  ExpectNullBuffer(buffer);
  ExpectTrimCalls({Fixed(20)});
}

TEST_P(ReadableStreamTest, OneBufferFullyConsume) {
  char* payload = kPayloadBuffer;
  auto stream = MakeStream(std::vector<FakeStream::QueuedBuffer>{
      {
          .start = Fixed(0),
          .end = Fixed(100),
          .payload = payload,
      },
  });

  {
    SCOPED_TRACE("ReadLock(0, 200)");
    {
      auto buffer = stream->ReadLock(rlctx, Fixed(0), 200);
      ExpectBuffer(buffer, Fixed(0), Fixed(100), payload);
    }
    ExpectTrimCalls({Fixed(100)});
  }

  {
    SCOPED_TRACE("ReadLock(100, 200)");
    {
      auto buffer = stream->ReadLock(rlctx, Fixed(100), 200);
      ExpectNullBuffer(buffer);
    }
    ExpectTrimCalls({Fixed(300)});
  }
}

TEST_P(ReadableStreamTest, OneBufferPartialConsume) {
  char* payload = kPayloadBuffer;
  auto stream = MakeStream(std::vector<FakeStream::QueuedBuffer>{
      {
          .start = Fixed(0),
          .end = Fixed(100),
          .payload = payload,
      },
  });

  {
    SCOPED_TRACE("ReadLock(0, 100), consume 0");
    {
      auto buffer = stream->ReadLock(rlctx, Fixed(0), 100);
      buffer->set_frames_consumed(0);
      ExpectBuffer(buffer, Fixed(0), Fixed(100), payload);
    }
    // When caching, we don't see any Trim calls until we consume the entire buffer.
    if (UseCaching()) {
      ExpectTrimCalls({});
    } else {
      ExpectTrimCalls({Fixed(0)});
    }
  }

  {
    // The prior ReadLock call did not consume any frames, so it is safe to repeat that call.
    SCOPED_TRACE("ReadLock(0, 100), consume 10");
    {
      auto buffer = stream->ReadLock(rlctx, Fixed(0), 100);
      buffer->set_frames_consumed(10);
      ExpectBuffer(buffer, Fixed(0), Fixed(100), payload);
    }
    // When caching, we don't see any Trim calls until we consume the entire buffer.
    if (UseCaching()) {
      ExpectTrimCalls({});
    } else {
      ExpectTrimCalls({Fixed(10)});
    }
  }

  {
    // The prior ReadLock call consumed through frame 10. Keep reading at that position.
    SCOPED_TRACE("ReadLock(10, 100), consume 10");
    {
      auto buffer = stream->ReadLock(rlctx, Fixed(10), 100);
      buffer->set_frames_consumed(10);
      ExpectBuffer(buffer, Fixed(10), Fixed(100), payload + 10 * kBytesPerFrame);
    }
    // When caching, we don't see any Trim calls until we consume the entire buffer.
    if (UseCaching()) {
      ExpectTrimCalls({});
    } else {
      ExpectTrimCalls({Fixed(20)});
    }
  }

  {
    // The prior ReadLock call consumed through frame 20. Skip ahead to frame 50.
    SCOPED_TRACE("ReadLock(50, 100), consume(10)");
    {
      auto buffer = stream->ReadLock(rlctx, Fixed(50), 100);
      buffer->set_frames_consumed(10);
      ExpectBuffer(buffer, Fixed(50), Fixed(100), payload + 50 * kBytesPerFrame);
    }
    // When caching, we don't see any Trim calls until we consume the entire buffer.
    if (UseCaching()) {
      ExpectTrimCalls({});
    } else {
      ExpectTrimCalls({Fixed(60)});
    }
  }

  {
    // The prior ReadLock call consumed through frame 60. Keep reading at that position.
    SCOPED_TRACE("ReadLock(60, 100), consume full");
    {
      auto buffer = stream->ReadLock(rlctx, Fixed(60), 40);
      ExpectBuffer(buffer, Fixed(60), Fixed(100), payload + 60 * kBytesPerFrame);
    }
    ExpectTrimCalls({Fixed(100)});
  }

  {
    // Buffer is exhausted.
    SCOPED_TRACE("ReadLock(100, 200)");
    {
      auto buffer = stream->ReadLock(rlctx, Fixed(100), 200);
      ExpectNullBuffer(buffer);
    }
    ExpectTrimCalls({Fixed(300)});
  }
}

TEST_P(ReadableStreamTest, MultipleBuffersFullyConsume) {
  char* payload_1 = kPayloadBuffer;
  char* payload_2 = payload_1 + 1000;
  char* payload_3 = payload_1 + 2000;
  auto stream = MakeStream(std::vector<FakeStream::QueuedBuffer>{
      {
          .start = Fixed(0),
          .end = Fixed(100),
          .payload = payload_1,
      },
      {
          .start = Fixed(100),
          .end = Fixed(200),
          .payload = payload_2,
      },
      {
          .start = Fixed(500),
          .end = Fixed(600),
          .payload = payload_3,
      },
  });

  {
    // No buffers have been consumed yet, so this call should return the first buffer.
    SCOPED_TRACE("ReadLock(0, 1000)");
    {
      auto buffer = stream->ReadLock(rlctx, Fixed(0), 1000);
      ExpectBuffer(buffer, Fixed(0), Fixed(100), payload_1);
    }
    ExpectTrimCalls({Fixed(100)});
  }

  {
    // The first buffer has been consumed, so this call should return the second buffer.
    SCOPED_TRACE("ReadLock(100, 1000)");
    {
      auto buffer = stream->ReadLock(rlctx, Fixed(100), 1000);
      ExpectBuffer(buffer, Fixed(100), Fixed(200), payload_2);
    }
    ExpectTrimCalls({Fixed(200)});
  }

  {
    // The second buffer has been consumed, so this call should return the third buffer.
    SCOPED_TRACE("ReadLock(200, 1000)");
    {
      auto buffer = stream->ReadLock(rlctx, Fixed(200), 1000);
      ExpectBuffer(buffer, Fixed(500), Fixed(600), payload_3);
    }
    ExpectTrimCalls({Fixed(600)});
  }

  {
    // There are no more buffers.
    SCOPED_TRACE("ReadLock(600, 1000)");
    {
      auto buffer = stream->ReadLock(rlctx, Fixed(600), 1000);
      ExpectNullBuffer(buffer);
    }
    ExpectTrimCalls({Fixed(1600)});
  }
}

TEST_P(ReadableStreamTest, MultipleBuffersPartialConsume) {
  char* payload_1 = kPayloadBuffer;
  char* payload_2 = payload_1 + 1000;
  auto stream = MakeStream(std::vector<FakeStream::QueuedBuffer>{
      {
          .start = Fixed(0),
          .end = Fixed(100),
          .payload = payload_1,
      },
      {
          .start = Fixed(100),
          .end = Fixed(200),
          .payload = payload_2,
      },
  });

  {
    SCOPED_TRACE("ReadLock(0, 1000), consume 50");
    {
      auto buffer = stream->ReadLock(rlctx, Fixed(0), 1000);
      buffer->set_frames_consumed(50);
      ExpectBuffer(buffer, Fixed(0), Fixed(100), payload_1);
    }
    // When caching, we don't see any Trim calls until we consume the entire buffer.
    if (UseCaching()) {
      ExpectTrimCalls({});
    } else {
      ExpectTrimCalls({Fixed(50)});
    }
  }

  {
    // The prior ReadLock consumed part of the first buffer.
    // This call returns the rest of that buffer.
    SCOPED_TRACE("ReadLock(50, 1000)");
    {
      auto buffer = stream->ReadLock(rlctx, Fixed(50), 1000);
      ExpectBuffer(buffer, Fixed(50), Fixed(100), payload_1 + 50 * kBytesPerFrame);
    }
    ExpectTrimCalls({Fixed(100)});
  }

  {
    // The prior ReadLock fully consumed the first buffer.
    // This call returns the second buffer.
    SCOPED_TRACE("ReadLock(100, 1000)");
    {
      auto buffer = stream->ReadLock(rlctx, Fixed(100), 1000);
      ExpectBuffer(buffer, Fixed(100), Fixed(200), payload_2);
    }
    ExpectTrimCalls({Fixed(200)});
  }

  {
    // No more buffers.
    SCOPED_TRACE("ReadLock(200, 1000)");
    {
      auto buffer = stream->ReadLock(rlctx, Fixed(200), 1000);
      ExpectNullBuffer(buffer);
    }
    ExpectTrimCalls({Fixed(1200)});
  }
}

TEST_P(ReadableStreamTest, FractionalFrames) {
  char* payload = kPayloadBuffer;
  auto stream = MakeStream(std::vector<FakeStream::QueuedBuffer>{
      {
          .start = Fixed(0) + ffl::FromRatio(5, 10),
          .end = Fixed(100) + ffl::FromRatio(5, 10),
          .payload = payload,
      },
  });

  {
    SCOPED_TRACE("ReadLock(1.6, 50)");
    {
      // Reqesting [1.6, 51.6) returns [1.5, 51.5).
      auto buffer = stream->ReadLock(rlctx, Fixed(1) + ffl::FromRatio(6, 10), 50);
      ExpectBuffer(buffer, Fixed(1) + ffl::FromRatio(5, 10), Fixed(51) + ffl::FromRatio(5, 10),
                   payload + kFormat.bytes_per_frame());
    }
    // When caching, we don't see any Trim calls until we consume the entire first packet.
    if (UseCaching()) {
      ExpectTrimCalls({});
    } else {
      ExpectTrimCalls({Fixed(51) + ffl::FromRatio(5, 10)});
    }
  }

  {
    SCOPED_TRACE("ReadLock(60.6, 30)");
    {
      // Reqesting [60.6, 90.6) returns [60.5, 90.5).
      auto buffer = stream->ReadLock(rlctx, Fixed(60) + ffl::FromRatio(6, 10), 30);
      ExpectBuffer(buffer, Fixed(60) + ffl::FromRatio(5, 10), Fixed(90) + ffl::FromRatio(5, 10),
                   payload + 60 * kFormat.bytes_per_frame());
    }
    // When caching, we don't see any Trim calls until we consume the entire first packet.
    if (UseCaching()) {
      ExpectTrimCalls({});
    } else {
      ExpectTrimCalls({Fixed(90) + ffl::FromRatio(5, 10)});
    }
  }

  {
    SCOPED_TRACE("ReadLock(99.6, 100)");
    {
      // Reqesting [99.6, 199.6) returns [99.5, 100.5).
      auto buffer = stream->ReadLock(rlctx, Fixed(99) + ffl::FromRatio(6, 10), 100);
      ExpectBuffer(buffer, Fixed(99) + ffl::FromRatio(5, 10), Fixed(100) + ffl::FromRatio(5, 10),
                   payload + 99 * kFormat.bytes_per_frame());
    }
    ExpectTrimCalls({Fixed(100) + ffl::FromRatio(5, 10)});
  }

  {
    SCOPED_TRACE("ReadLock(100.5, 100)");
    {
      auto buffer = stream->ReadLock(rlctx, Fixed(100) + ffl::FromRatio(5, 10), 100);
      ExpectNullBuffer(buffer);
    }
    ExpectTrimCalls({Fixed(200) + ffl::FromRatio(5, 10)});
  }
}

TEST_P(ReadableStreamTest, Reset) {
  char* payload_1 = kPayloadBuffer;
  char* payload_2 = payload_1 + 1000;
  auto stream = MakeStream(std::vector<FakeStream::QueuedBuffer>{
      {
          .start = Fixed(100),
          .end = Fixed(110),
          .payload = payload_1,
      },
  });

  {
    SCOPED_TRACE("ReadLock(100, 500)");
    {
      auto buffer = stream->ReadLock(rlctx, Fixed(100), 500);
      ExpectBuffer(buffer, Fixed(100), Fixed(110), payload_1);
    }
    ExpectTrimCalls({Fixed(110)});
    EXPECT_EQ(stream->NextAvailableFrame(), Fixed(110));
  }

  fake_stream().set_timeline_function(
      TimelineFunction(99, 0, Fixed(kFormat.frames_per_second()).raw_value(), zx::sec(1).get()));

  fake_stream().PushBuffer({
      .start = Fixed(50),
      .end = Fixed(60),
      .payload = payload_2,
  });

  EXPECT_EQ(stream->NextAvailableFrame(), std::nullopt);

  // Time reset: going backwards in position is ok.
  {
    SCOPED_TRACE("ReadLock(0, 1000)");
    {
      auto buffer = stream->ReadLock(rlctx, Fixed(0), 1000);
      ExpectBuffer(buffer, Fixed(50), Fixed(60), payload_2);
    }
    ExpectTrimCalls({Fixed(60)});
    EXPECT_EQ(stream->NextAvailableFrame(), Fixed(60));
  }
}

INSTANTIATE_TEST_SUITE_P(ReadableStreamTestPipelines, ReadableStreamTest,
                         ::testing::Values(FakeStreamWithCaching, FakeStreamWithoutCaching,
                                           FakeStreamWithCachingThenPassthrough,
                                           FakeStreamWithoutCachingThenPassthrough),
                         PipelineParamToString);

}  // namespace media::audio

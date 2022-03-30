// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/mixer_service/mix/pipeline_stage.h"

#include <lib/fit/defer.h>

#include <memory>

#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/audio_clock.h"
#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/timeline/timeline_function.h"
#include "src/media/audio/mixer_service/common/basic_types.h"
#include "src/media/audio/mixer_service/mix/packet.h"

namespace media_audio_mixer_service {
namespace {

using ::fuchsia_mediastreams::wire::AudioSampleFormat;
using ::media::TimelineFunction;
using ::media::audio::AudioClock;

const auto kFormat = Format::CreateOrDie({AudioSampleFormat::kSigned16, 1, 48000});
const auto kBytesPerFrame = kFormat.bytes_per_frame();

// These tests need to check the value of a Buffer's payload pointer. Ideally we might put frame 0
// at nullptr, but if we did that, computing the address of frame 1 would require adding an offset
// to nullptr, which is undefined behavior. So instead we preallocate a buffer so we have a pointer
// to actually memory, which keeps UBSan happy.
char* const kPayloadBuffer = new char[4096];

// Fake stage that acts like a packet queue, but can be configured to use either `MakeCachedBuffer`
// or `MakeUncachedBuffer`.
class FakeStage : public PipelineStage {
 public:
  struct QueuedBuffer {
    Fixed start;
    Fixed end;
    char* payload;
  };

  FakeStage(bool use_cache, std::vector<QueuedBuffer>&& buffers)
      : PipelineStage("FakeStage", kFormat),
        use_cache_(use_cache),
        buffers_(std::move(buffers)),
        audio_clock_(AudioClock::ClientFixed(media::audio::clock::CloneOfMonotonic())),
        timeline_function_(TimelineFunction(0, 0, Fixed(kFormat.frames_per_second()).raw_value(),
                                            zx::sec(1).get())) {}

  // TODO(fxbug.dev/87651): Use this instead of the constructor.
  void AddSource(PipelineStagePtr src) override {}
  void RemoveSource(PipelineStagePtr src) override {}

  TimelineFunction ref_time_to_frac_presentation_frame() const override {
    return timeline_function_;
  }
  AudioClock& reference_clock() override { return audio_clock_; }

  std::optional<Buffer> ReadImpl(Fixed start_frame, int64_t frame_count) override {
    if (cached_end_ && start_frame < *cached_end_) {
      ADD_FAILURE() << ffl::String::DecRational
                    << "Read not handled by cache? frame=" << start_frame
                    << " frame_count=" << frame_count << " cached_end=" << *cached_end_;
    }

    if (buffers_.empty()) {
      return std::nullopt;
    }

    Fixed length = buffers_.front().end - buffers_.front().start;
    FX_CHECK(length.Fraction() == Fixed(0));

    auto packet = Packet(Packet::Args{
        .format = format(),
        .start = buffers_.front().start,
        .length = length.Floor(),
        .payload = buffers_.front().payload,
    });
    auto isect = packet.IntersectionWith(start_frame, frame_count);
    if (!isect) {
      return std::nullopt;
    }

    if (use_cache_) {
      // When caching, the start frame must intersect the request, but we can cache an arbitrary
      // number of frames. See comments for `MakeCachedBuffer`.
      const Fixed packet_end = packet.start() + Fixed(packet.length());
      const int64_t length = Fixed(packet_end - isect->start()).Floor();
      // When caching, we should not see a `ReadImpl` call that intersects the buffer we are
      // returning (all of those intersections should be handled by the cache).
      cached_end_ = isect->start() + Fixed(isect->length());
      return MakeCachedBuffer(isect->start(), length, isect->payload());
    }
    return MakeUncachedBuffer(isect->start(), isect->length(), isect->payload());
  }

  void AdvanceImpl(Fixed frame) override {
    if (advance_calls_.empty() || frame > advance_calls_.back()) {
      advance_calls_.push_back(frame);
    }
    // Release old buffers.
    while (!buffers_.empty() && buffers_[0].end <= frame) {
      buffers_.erase(buffers_.begin());
    }
  }

  std::vector<Fixed>& advance_calls() { return advance_calls_; }

  void PushBuffer(QueuedBuffer buffer) { buffers_.push_back(buffer); }

 private:
  const bool use_cache_;
  std::vector<QueuedBuffer> buffers_;
  std::vector<Fixed> advance_calls_;
  std::optional<Fixed> cached_end_;
  AudioClock audio_clock_;
  TimelineFunction timeline_function_;
};

// No-op passthrough stage that wraps a source stage via using `ForwardBuffer`.
class PassthroughStage : public PipelineStage {
 public:
  explicit PassthroughStage(std::shared_ptr<FakeStage> src)
      : PipelineStage("PassthroughStage", src->format()), src_(src) {}

  // TODO(fxbug.dev/87651): Use this instead of the constructor.
  void AddSource(PipelineStagePtr src) override {}
  void RemoveSource(PipelineStagePtr src) override {}

  TimelineFunction ref_time_to_frac_presentation_frame() const override {
    return src_->ref_time_to_frac_presentation_frame();
  }
  AudioClock& reference_clock() override { return src_->reference_clock(); }

  std::optional<Buffer> ReadImpl(Fixed start_frame, int64_t frame_count) override {
    return ForwardBuffer(src_->Read(start_frame, frame_count));
  }

  void AdvanceImpl(Fixed frame) override { src_->Advance(frame); }

 private:
  std::shared_ptr<FakeStage> src_;
};

// All tests in this file can be run against four pipelines.
enum PipelineType {
  FakeStageWithCaching,
  FakeStageWithoutCaching,
  FakeStageWithCachingThenPassthrough,
  FakeStageWithoutCachingThenPassthrough,
};

std::string PipelineParamToString(const ::testing::TestParamInfo<PipelineType>& info) {
  switch (info.param) {
    case FakeStageWithCaching:
      return "FakeStageWithCaching";
    case FakeStageWithoutCaching:
      return "FakeStageWithoutCaching";
    case FakeStageWithCachingThenPassthrough:
      return "FakeStageWithCachingThenPassthrough";
    case FakeStageWithoutCachingThenPassthrough:
      return "FakeStageWithoutCachingThenPassthrough";
    default:
      FX_CHECK(false) << info.param;
  }
}

class PipelineStageTest : public ::testing::TestWithParam<PipelineType> {
 protected:
  bool UseCaching() {
    return GetParam() == FakeStageWithCaching || GetParam() == FakeStageWithCachingThenPassthrough;
  }

  std::shared_ptr<PipelineStage> MakeStage(std::vector<FakeStage::QueuedBuffer>&& buffers) {
    switch (GetParam()) {
      case FakeStageWithCaching:
      case FakeStageWithCachingThenPassthrough:
        fake_stage_ = std::make_shared<FakeStage>(true /* use_cache */, std::move(buffers));
        break;
      case FakeStageWithoutCaching:
      case FakeStageWithoutCachingThenPassthrough:
        fake_stage_ = std::make_shared<FakeStage>(false /* use_cache */, std::move(buffers));
        break;
      default:
        FX_CHECK(false) << GetParam();
    }
    switch (GetParam()) {
      case FakeStageWithCaching:
      case FakeStageWithoutCaching:
        return fake_stage_;
      case FakeStageWithCachingThenPassthrough:
      case FakeStageWithoutCachingThenPassthrough:
        return std::make_shared<PassthroughStage>(fake_stage_);
    }
  }

  void ExpectNullBuffer(const std::optional<PipelineStage::Buffer>& buffer) {
    EXPECT_FALSE(buffer) << "start=" << ffl::String(buffer->start()).c_str()
                         << " end=" << ffl::String(buffer->end()).c_str();
  }

  void ExpectBuffer(const std::optional<PipelineStage::Buffer>& buffer, Fixed want_start,
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

  void ExpectAdvanceCalls(const std::vector<Fixed>& want) {
    auto cleanup = fit::defer([this]() { fake_stage_->advance_calls().clear(); });

    if (fake_stage_->advance_calls() == want) {
      return;
    }

    std::ostringstream ostr;
    ostr << "got = {";
    for (auto frame : fake_stage_->advance_calls()) {
      ostr << " " << ffl::String(frame).c_str();
    }
    ostr << "}, want = {";
    for (auto frame : want) {
      ostr << " " << ffl::String(frame).c_str();
    }
    ostr << "}";
    ADD_FAILURE() << ostr.str();
  }

 private:
  std::shared_ptr<FakeStage> fake_stage_;
};

TEST_P(PipelineStageTest, EmptySource) {
  auto stage = MakeStage(std::vector<FakeStage::QueuedBuffer>());
  auto buffer = stage->Read(Fixed(0), 20);
  ExpectNullBuffer(buffer);
  ExpectAdvanceCalls({Fixed(20)});
}

TEST_P(PipelineStageTest, OneBufferFullyConsume) {
  char* payload = kPayloadBuffer;
  auto stage = MakeStage(std::vector<FakeStage::QueuedBuffer>{
      {
          .start = Fixed(0),
          .end = Fixed(100),
          .payload = payload,
      },
  });

  {
    SCOPED_TRACE("Read(0, 200)");
    {
      auto buffer = stage->Read(Fixed(0), 200);
      ExpectBuffer(buffer, Fixed(0), Fixed(100), payload);
    }
    ExpectAdvanceCalls({Fixed(100)});
  }

  {
    SCOPED_TRACE("Read(100, 200)");
    {
      auto buffer = stage->Read(Fixed(100), 200);
      ExpectNullBuffer(buffer);
    }
    ExpectAdvanceCalls({Fixed(300)});
  }
}

TEST_P(PipelineStageTest, OneBufferPartialConsume) {
  char* payload = kPayloadBuffer;
  auto stage = MakeStage(std::vector<FakeStage::QueuedBuffer>{
      {
          .start = Fixed(0),
          .end = Fixed(100),
          .payload = payload,
      },
  });

  {
    SCOPED_TRACE("Read(0, 100), consume 0");
    {
      auto buffer = stage->Read(Fixed(0), 100);
      buffer->set_frames_consumed(0);
      ExpectBuffer(buffer, Fixed(0), Fixed(100), payload);
    }
    // When caching, we don't see any `Advance` calls until we consume the entire buffer.
    if (UseCaching()) {
      ExpectAdvanceCalls({});
    } else {
      ExpectAdvanceCalls({Fixed(0)});
    }
  }

  {
    // The prior `Read` call did not consume any frames, so it is safe to repeat that call.
    SCOPED_TRACE("Read(0, 100), consume 10");
    {
      auto buffer = stage->Read(Fixed(0), 100);
      buffer->set_frames_consumed(10);
      ExpectBuffer(buffer, Fixed(0), Fixed(100), payload);
    }
    // When caching, we don't see any `Advance` calls until we consume the entire buffer.
    if (UseCaching()) {
      ExpectAdvanceCalls({});
    } else {
      ExpectAdvanceCalls({Fixed(10)});
    }
  }

  {
    // The prior `Read` call consumed through frame 10. Keep reading at that position.
    SCOPED_TRACE("Read(10, 100), consume 10");
    {
      auto buffer = stage->Read(Fixed(10), 100);
      buffer->set_frames_consumed(10);
      ExpectBuffer(buffer, Fixed(10), Fixed(100), payload + 10 * kBytesPerFrame);
    }
    // When caching, we don't see any `Advance` calls until we consume the entire buffer.
    if (UseCaching()) {
      ExpectAdvanceCalls({});
    } else {
      ExpectAdvanceCalls({Fixed(20)});
    }
  }

  {
    // The prior `Read` call consumed through frame 20. Skip ahead to frame 50.
    SCOPED_TRACE("Read(50, 100), consume(10)");
    {
      auto buffer = stage->Read(Fixed(50), 100);
      buffer->set_frames_consumed(10);
      ExpectBuffer(buffer, Fixed(50), Fixed(100), payload + 50 * kBytesPerFrame);
    }
    // When caching, we don't see any `Advance` calls until we consume the entire buffer.
    if (UseCaching()) {
      ExpectAdvanceCalls({});
    } else {
      ExpectAdvanceCalls({Fixed(60)});
    }
  }

  {
    // The prior `Read` call consumed through frame 60. Keep reading at that position.
    SCOPED_TRACE("Read(60, 100), consume full");
    {
      auto buffer = stage->Read(Fixed(60), 40);
      ExpectBuffer(buffer, Fixed(60), Fixed(100), payload + 60 * kBytesPerFrame);
    }
    ExpectAdvanceCalls({Fixed(100)});
  }

  {
    // Buffer is exhausted.
    SCOPED_TRACE("Read(100, 200)");
    {
      auto buffer = stage->Read(Fixed(100), 200);
      ExpectNullBuffer(buffer);
    }
    ExpectAdvanceCalls({Fixed(300)});
  }
}

TEST_P(PipelineStageTest, MultipleBuffersFullyConsume) {
  char* payload_1 = kPayloadBuffer;
  char* payload_2 = payload_1 + 1000;
  char* payload_3 = payload_1 + 2000;
  auto stage = MakeStage(std::vector<FakeStage::QueuedBuffer>{
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
    SCOPED_TRACE("Read(0, 1000)");
    {
      auto buffer = stage->Read(Fixed(0), 1000);
      ExpectBuffer(buffer, Fixed(0), Fixed(100), payload_1);
    }
    ExpectAdvanceCalls({Fixed(100)});
  }

  {
    // The first buffer has been consumed, so this call should return the second buffer.
    SCOPED_TRACE("Read(100, 1000)");
    {
      auto buffer = stage->Read(Fixed(100), 1000);
      ExpectBuffer(buffer, Fixed(100), Fixed(200), payload_2);
    }
    ExpectAdvanceCalls({Fixed(200)});
  }

  {
    // The second buffer has been consumed, so this call should return the third buffer.
    SCOPED_TRACE("Read(200, 1000)");
    {
      auto buffer = stage->Read(Fixed(200), 1000);
      ExpectBuffer(buffer, Fixed(500), Fixed(600), payload_3);
    }
    ExpectAdvanceCalls({Fixed(600)});
  }

  {
    // There are no more buffers.
    SCOPED_TRACE("Read(600, 1000)");
    {
      auto buffer = stage->Read(Fixed(600), 1000);
      ExpectNullBuffer(buffer);
    }
    ExpectAdvanceCalls({Fixed(1600)});
  }
}

TEST_P(PipelineStageTest, MultipleBuffersPartialConsume) {
  char* payload_1 = kPayloadBuffer;
  char* payload_2 = payload_1 + 1000;
  auto stage = MakeStage(std::vector<FakeStage::QueuedBuffer>{
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
    SCOPED_TRACE("Read(0, 1000), consume 50");
    {
      auto buffer = stage->Read(Fixed(0), 1000);
      buffer->set_frames_consumed(50);
      ExpectBuffer(buffer, Fixed(0), Fixed(100), payload_1);
    }
    // When caching, we don't see any `Advance` calls until we consume the entire buffer.
    if (UseCaching()) {
      ExpectAdvanceCalls({});
    } else {
      ExpectAdvanceCalls({Fixed(50)});
    }
  }

  {
    // The prior `Read` consumed part of the first buffer.
    // This call returns the rest of that buffer.
    SCOPED_TRACE("Read(50, 1000)");
    {
      auto buffer = stage->Read(Fixed(50), 1000);
      ExpectBuffer(buffer, Fixed(50), Fixed(100), payload_1 + 50 * kBytesPerFrame);
    }
    ExpectAdvanceCalls({Fixed(100)});
  }

  {
    // The prior `Read` fully consumed the first buffer.
    // This call returns the second buffer.
    SCOPED_TRACE("Read(100, 1000)");
    {
      auto buffer = stage->Read(Fixed(100), 1000);
      ExpectBuffer(buffer, Fixed(100), Fixed(200), payload_2);
    }
    ExpectAdvanceCalls({Fixed(200)});
  }

  {
    // No more buffers.
    SCOPED_TRACE("Read(200, 1000)");
    {
      auto buffer = stage->Read(Fixed(200), 1000);
      ExpectNullBuffer(buffer);
    }
    ExpectAdvanceCalls({Fixed(1200)});
  }
}

TEST_P(PipelineStageTest, FractionalFrames) {
  char* payload = kPayloadBuffer;
  auto stage = MakeStage(std::vector<FakeStage::QueuedBuffer>{
      {
          .start = Fixed(0) + ffl::FromRatio(5, 10),
          .end = Fixed(100) + ffl::FromRatio(5, 10),
          .payload = payload,
      },
  });

  {
    SCOPED_TRACE("Read(1.6, 50)");
    {
      // Reqesting [1.6, 51.6) returns [1.5, 51.5).
      auto buffer = stage->Read(Fixed(1) + ffl::FromRatio(6, 10), 50);
      ExpectBuffer(buffer, Fixed(1) + ffl::FromRatio(5, 10), Fixed(51) + ffl::FromRatio(5, 10),
                   payload + kFormat.bytes_per_frame());
    }
    // When caching, we don't see any `Advance` calls until we consume the entire first packet.
    if (UseCaching()) {
      ExpectAdvanceCalls({});
    } else {
      ExpectAdvanceCalls({Fixed(51) + ffl::FromRatio(5, 10)});
    }
  }

  {
    SCOPED_TRACE("Read(60.6, 30)");
    {
      // Reqesting [60.6, 90.6) returns [60.5, 90.5).
      auto buffer = stage->Read(Fixed(60) + ffl::FromRatio(6, 10), 30);
      ExpectBuffer(buffer, Fixed(60) + ffl::FromRatio(5, 10), Fixed(90) + ffl::FromRatio(5, 10),
                   payload + 60 * kFormat.bytes_per_frame());
    }
    // When caching, we don't see any `Advance` calls until we consume the entire first packet.
    if (UseCaching()) {
      ExpectAdvanceCalls({});
    } else {
      ExpectAdvanceCalls({Fixed(90) + ffl::FromRatio(5, 10)});
    }
  }

  {
    SCOPED_TRACE("Read(99.6, 100)");
    {
      // Reqesting [99.6, 199.6) returns [99.5, 100.5).
      auto buffer = stage->Read(Fixed(99) + ffl::FromRatio(6, 10), 100);
      ExpectBuffer(buffer, Fixed(99) + ffl::FromRatio(5, 10), Fixed(100) + ffl::FromRatio(5, 10),
                   payload + 99 * kFormat.bytes_per_frame());
    }
    ExpectAdvanceCalls({Fixed(100) + ffl::FromRatio(5, 10)});
  }

  {
    SCOPED_TRACE("Read(100.5, 100)");
    {
      auto buffer = stage->Read(Fixed(100) + ffl::FromRatio(5, 10), 100);
      ExpectNullBuffer(buffer);
    }
    ExpectAdvanceCalls({Fixed(200) + ffl::FromRatio(5, 10)});
  }
}

INSTANTIATE_TEST_SUITE_P(PipelineStageTestPipelines, PipelineStageTest,
                         testing::Values(FakeStageWithCaching, FakeStageWithoutCaching,
                                         FakeStageWithCachingThenPassthrough,
                                         FakeStageWithoutCachingThenPassthrough),
                         PipelineParamToString);

}  // namespace
}  // namespace media_audio_mixer_service

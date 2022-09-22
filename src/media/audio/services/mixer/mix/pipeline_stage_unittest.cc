// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/pipeline_stage.h"

#include <lib/fit/defer.h>

#include <memory>
#include <unordered_set>

#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/mix/mix_job_context.h"
#include "src/media/audio/services/mixer/mix/packet_view.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"

namespace media_audio {
namespace {

using ::fuchsia_mediastreams::wire::AudioSampleFormat;

const auto kFormat = Format::CreateOrDie({AudioSampleFormat::kSigned16, 1, 48000});
const auto kBytesPerFrame = kFormat.bytes_per_frame();

// These tests need to check the value of a Packet's payload pointer. Ideally we might put frame 0
// at nullptr, but if we did that, computing the address of frame 1 would require adding an offset
// to nullptr, which is undefined behavior. So instead we preallocate a packet so we have a pointer
// to actually memory, which keeps UBSan happy.
char* const kPayloadPacket = new char[4096];

// Fake stage that acts like a packet queue, but can be configured to use either `MakeCachedPacket`
// or `MakeUncachedPacket`.
class FakeStage : public PipelineStage {
 public:
  struct QueuedPacket {
    Fixed start;
    Fixed end;
    char* payload;
  };

  FakeStage(bool use_cache, std::vector<QueuedPacket>&& packets)
      : PipelineStage("FakeStage", kFormat, DefaultClock()),
        use_cache_(use_cache),
        packets_(std::move(packets)) {}

  // TODO(fxbug.dev/87651): Use this instead of the constructor.
  void AddSource(PipelineStagePtr source, AddSourceOptions options) override {}
  void RemoveSource(PipelineStagePtr source) override {}
  void UpdatePresentationTimeToFracFrame(std::optional<TimelineFunction> f) override {
    set_presentation_time_to_frac_frame(f);
  }

  void AdvanceSelfImpl(Fixed frame) override {
    if (advance_calls_.empty() || frame > advance_calls_.back()) {
      advance_calls_.push_back(frame);
    }
    // Release old packets.
    while (!packets_.empty() && packets_[0].end <= frame) {
      packets_.erase(packets_.begin());
    }
  }

  void AdvanceSourcesImpl(MixJobContext& ctx, Fixed frame) override {}

  std::optional<Packet> ReadImpl(MixJobContext& ctx, Fixed start_frame,
                                 int64_t frame_count) override {
    if (cached_end_ && start_frame < *cached_end_) {
      ADD_FAILURE() << ffl::String::DecRational
                    << "Read not handled by cache? frame=" << start_frame
                    << " frame_count=" << frame_count << " cached_end=" << *cached_end_;
    }

    if (packets_.empty()) {
      return std::nullopt;
    }

    Fixed length = packets_.front().end - packets_.front().start;
    FX_CHECK(length.Fraction() == Fixed(0));

    auto packet = PacketView({
        .format = format(),
        .start = packets_.front().start,
        .length = length.Floor(),
        .payload = packets_.front().payload,
    });
    auto isect = packet.IntersectionWith(start_frame, frame_count);
    if (!isect) {
      return std::nullopt;
    }

    if (use_cache_) {
      // When caching, the start frame must intersect the request, but we can cache an arbitrary
      // number of frames. See comments for `MakeCachedPacket`.
      const Fixed packet_end = packet.start() + Fixed(packet.length());
      const int64_t length = Fixed(packet_end - isect->start()).Floor();
      // When caching, we should not see a `ReadImpl` call that intersects the packet we are
      // returning (all of those intersections should be handled by the cache).
      cached_end_ = isect->start() + Fixed(isect->length());
      return MakeCachedPacket(isect->start(), length, isect->payload());
    }
    return MakeUncachedPacket(isect->start(), isect->length(), isect->payload());
  }

  std::vector<Fixed>& advance_calls() { return advance_calls_; }

  void PushPacket(QueuedPacket packet) { packets_.push_back(packet); }

 private:
  const bool use_cache_;
  std::vector<QueuedPacket> packets_;
  std::vector<Fixed> advance_calls_;
  std::optional<Fixed> cached_end_;
};

// No-op passthrough stage that wraps a source stage via using `ForwardPacket`.
class PassthroughStage : public PipelineStage {
 public:
  explicit PassthroughStage(std::shared_ptr<FakeStage> source)
      : PipelineStage("PassthroughStage", source->format(), DefaultClock()), source_(source) {}

  // TODO(fxbug.dev/87651): Use this instead of the constructor.
  void AddSource(PipelineStagePtr source, AddSourceOptions options) override {}
  void RemoveSource(PipelineStagePtr source) override {}
  void UpdatePresentationTimeToFracFrame(std::optional<TimelineFunction> f) override {
    set_presentation_time_to_frac_frame(f);
    source_->UpdatePresentationTimeToFracFrame(f);
  }

  void AdvanceSelfImpl(Fixed frame) override {}
  void AdvanceSourcesImpl(MixJobContext& ctx, Fixed frame) override {
    source_->Advance(ctx, frame);
  }

  std::optional<Packet> ReadImpl(MixJobContext& ctx, Fixed start_frame,
                                 int64_t frame_count) override {
    return ForwardPacket(source_->Read(ctx, start_frame, frame_count));
  }

 private:
  std::shared_ptr<FakeStage> source_;
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

  std::shared_ptr<PipelineStage> MakeStage(std::vector<FakeStage::QueuedPacket>&& packets) {
    switch (GetParam()) {
      case FakeStageWithCaching:
      case FakeStageWithCachingThenPassthrough:
        fake_stage_ = std::make_shared<FakeStage>(true /* use_cache */, std::move(packets));
        break;
      case FakeStageWithoutCaching:
      case FakeStageWithoutCachingThenPassthrough:
        fake_stage_ = std::make_shared<FakeStage>(false /* use_cache */, std::move(packets));
        break;
      default:
        FX_CHECK(false) << GetParam();
    }

    std::shared_ptr<PipelineStage> stage = fake_stage_;
    if (GetParam() == FakeStageWithCachingThenPassthrough ||
        GetParam() == FakeStageWithoutCachingThenPassthrough) {
      stage = std::make_shared<PassthroughStage>(fake_stage_);
    }

    stage->UpdatePresentationTimeToFracFrame(DefaultPresentationTimeToFracFrame(stage->format()));
    return stage;
  }

  void ExpectNullPacket(const std::optional<PipelineStage::Packet>& packet) {
    EXPECT_FALSE(packet) << "start=" << ffl::String(packet->start()).c_str()
                         << " end=" << ffl::String(packet->end()).c_str();
  }

  void ExpectPacket(const std::optional<PipelineStage::Packet>& packet, Fixed want_start,
                    Fixed want_end, char* want_payload) {
    ASSERT_TRUE(packet);
    EXPECT_EQ(reinterpret_cast<void*>(want_payload), packet->payload());
    if (want_start != packet->start()) {
      ADD_FAILURE() << "want_start=" << ffl::String(want_start).c_str()
                    << " start=" << ffl::String(packet->start()).c_str();
    }
    if (want_end != packet->end()) {
      ADD_FAILURE() << "want_end=" << ffl::String(want_end).c_str()
                    << " end=" << ffl::String(packet->end()).c_str();
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
  auto stage = MakeStage(std::vector<FakeStage::QueuedPacket>());
  auto packet = stage->Read(DefaultCtx(), Fixed(0), 20);
  ExpectNullPacket(packet);
  ExpectAdvanceCalls({Fixed(0), Fixed(20)});
}

TEST_P(PipelineStageTest, OnePacketFullyConsume) {
  char* payload = kPayloadPacket;
  auto stage = MakeStage(std::vector<FakeStage::QueuedPacket>{
      {
          .start = Fixed(0),
          .end = Fixed(100),
          .payload = payload,
      },
  });

  {
    SCOPED_TRACE("Read(0, 200)");
    {
      auto packet = stage->Read(DefaultCtx(), Fixed(0), 200);
      ExpectPacket(packet, Fixed(0), Fixed(100), payload);
    }
    ExpectAdvanceCalls({Fixed(0), Fixed(100)});
  }

  {
    SCOPED_TRACE("Read(100, 200)");
    {
      auto packet = stage->Read(DefaultCtx(), Fixed(100), 200);
      ExpectNullPacket(packet);
    }
    ExpectAdvanceCalls({Fixed(300)});
  }
}

TEST_P(PipelineStageTest, OnePacketPartialConsume) {
  char* payload = kPayloadPacket;
  auto stage = MakeStage(std::vector<FakeStage::QueuedPacket>{
      {
          .start = Fixed(0),
          .end = Fixed(100),
          .payload = payload,
      },
  });

  {
    SCOPED_TRACE("Read(0, 100), consume 0");
    {
      auto packet = stage->Read(DefaultCtx(), Fixed(0), 100);
      packet->set_frames_consumed(0);
      ExpectPacket(packet, Fixed(0), Fixed(100), payload);
    }
    ExpectAdvanceCalls({Fixed(0)});
  }

  {
    // The prior `Read` call did not consume any frames, so it is safe to repeat that call.
    SCOPED_TRACE("Read(0, 100), consume 10");
    {
      auto packet = stage->Read(DefaultCtx(), Fixed(0), 100);
      packet->set_frames_consumed(10);
      ExpectPacket(packet, Fixed(0), Fixed(100), payload);
    }
    // When caching, we don't see any `Advance` calls until we consume the entire packet.
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
      auto packet = stage->Read(DefaultCtx(), Fixed(10), 100);
      packet->set_frames_consumed(10);
      ExpectPacket(packet, Fixed(10), Fixed(100), payload + 10 * kBytesPerFrame);
    }
    // When caching, we don't see any `Advance` calls until we consume the entire packet.
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
      auto packet = stage->Read(DefaultCtx(), Fixed(50), 100);
      packet->set_frames_consumed(10);
      ExpectPacket(packet, Fixed(50), Fixed(100), payload + 50 * kBytesPerFrame);
    }
    // When caching, we don't see any `Advance` calls until we consume the entire packet.
    if (UseCaching()) {
      ExpectAdvanceCalls({});
    } else {
      // Skipping to frame 50 results in an additional `Advance` call.
      ExpectAdvanceCalls({Fixed(50), Fixed(60)});
    }
  }

  {
    // The prior `Read` call consumed through frame 60. Keep reading at that position.
    SCOPED_TRACE("Read(60, 100), consume full");
    {
      auto packet = stage->Read(DefaultCtx(), Fixed(60), 40);
      ExpectPacket(packet, Fixed(60), Fixed(100), payload + 60 * kBytesPerFrame);
    }
    ExpectAdvanceCalls({Fixed(100)});
  }

  {
    // Packet is exhausted.
    SCOPED_TRACE("Read(100, 200)");
    {
      auto packet = stage->Read(DefaultCtx(), Fixed(100), 200);
      ExpectNullPacket(packet);
    }
    ExpectAdvanceCalls({Fixed(300)});
  }
}

TEST_P(PipelineStageTest, MultiplePacketsFullyConsume) {
  char* payload_1 = kPayloadPacket;
  char* payload_2 = payload_1 + 1000;
  char* payload_3 = payload_1 + 2000;
  auto stage = MakeStage(std::vector<FakeStage::QueuedPacket>{
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
    // No packets have been consumed yet, so this call should return the first packet.
    SCOPED_TRACE("Read(0, 1000)");
    {
      auto packet = stage->Read(DefaultCtx(), Fixed(0), 1000);
      ExpectPacket(packet, Fixed(0), Fixed(100), payload_1);
    }
    ExpectAdvanceCalls({Fixed(0), Fixed(100)});
  }

  {
    // The first packet has been consumed, so this call should return the second packet.
    SCOPED_TRACE("Read(100, 1000)");
    {
      auto packet = stage->Read(DefaultCtx(), Fixed(100), 1000);
      ExpectPacket(packet, Fixed(100), Fixed(200), payload_2);
    }
    ExpectAdvanceCalls({Fixed(200)});
  }

  {
    // The second packet has been consumed, so this call should return the third packet.
    SCOPED_TRACE("Read(200, 1000)");
    {
      auto packet = stage->Read(DefaultCtx(), Fixed(200), 1000);
      ExpectPacket(packet, Fixed(500), Fixed(600), payload_3);
    }
    ExpectAdvanceCalls({Fixed(600)});
  }

  {
    // There are no more packets.
    SCOPED_TRACE("Read(600, 1000)");
    {
      auto packet = stage->Read(DefaultCtx(), Fixed(600), 1000);
      ExpectNullPacket(packet);
    }
    ExpectAdvanceCalls({Fixed(1600)});
  }
}

TEST_P(PipelineStageTest, MultiplePacketsPartialConsume) {
  char* payload_1 = kPayloadPacket;
  char* payload_2 = payload_1 + 1000;
  auto stage = MakeStage(std::vector<FakeStage::QueuedPacket>{
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
      auto packet = stage->Read(DefaultCtx(), Fixed(0), 1000);
      packet->set_frames_consumed(50);
      ExpectPacket(packet, Fixed(0), Fixed(100), payload_1);
    }
    // We always advance to the first read frame.
    // When caching, we don't see any more `Advance` calls until we consume the entire packet.
    if (UseCaching()) {
      ExpectAdvanceCalls({Fixed(0)});
    } else {
      ExpectAdvanceCalls({Fixed(0), Fixed(50)});
    }
  }

  {
    // The prior `Read` consumed part of the first packet.
    // This call returns the rest of that packet.
    SCOPED_TRACE("Read(50, 1000)");
    {
      auto packet = stage->Read(DefaultCtx(), Fixed(50), 1000);
      ExpectPacket(packet, Fixed(50), Fixed(100), payload_1 + 50 * kBytesPerFrame);
    }
    ExpectAdvanceCalls({Fixed(100)});
  }

  {
    // The prior `Read` fully consumed the first packet.
    // This call returns the second packet.
    SCOPED_TRACE("Read(100, 1000)");
    {
      auto packet = stage->Read(DefaultCtx(), Fixed(100), 1000);
      ExpectPacket(packet, Fixed(100), Fixed(200), payload_2);
    }
    ExpectAdvanceCalls({Fixed(200)});
  }

  {
    // No more packets.
    SCOPED_TRACE("Read(200, 1000)");
    {
      auto packet = stage->Read(DefaultCtx(), Fixed(200), 1000);
      ExpectNullPacket(packet);
    }
    ExpectAdvanceCalls({Fixed(1200)});
  }
}

TEST_P(PipelineStageTest, FractionalFrames) {
  char* payload = kPayloadPacket;
  auto stage = MakeStage(std::vector<FakeStage::QueuedPacket>{
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
      auto packet = stage->Read(DefaultCtx(), Fixed(1) + ffl::FromRatio(6, 10), 50);
      ExpectPacket(packet, Fixed(1) + ffl::FromRatio(5, 10), Fixed(51) + ffl::FromRatio(5, 10),
                   payload + kFormat.bytes_per_frame());
    }
    if (UseCaching()) {
      ExpectAdvanceCalls({Fixed(1) + ffl::FromRatio(6, 10)});
    } else {
      ExpectAdvanceCalls({Fixed(1) + ffl::FromRatio(6, 10), Fixed(51) + ffl::FromRatio(5, 10)});
    }
  }

  {
    SCOPED_TRACE("Read(60.6, 30)");
    {
      // Reqesting [60.6, 90.6) returns [60.5, 90.5).
      auto packet = stage->Read(DefaultCtx(), Fixed(60) + ffl::FromRatio(6, 10), 30);
      ExpectPacket(packet, Fixed(60) + ffl::FromRatio(5, 10), Fixed(90) + ffl::FromRatio(5, 10),
                   payload + 60 * kFormat.bytes_per_frame());
    }
    // When caching, we don't see any `Advance` calls until we consume the entire first packet.
    if (UseCaching()) {
      ExpectAdvanceCalls({});
    } else {
      ExpectAdvanceCalls({Fixed(60) + ffl::FromRatio(6, 10), Fixed(90) + ffl::FromRatio(5, 10)});
    }
  }

  {
    SCOPED_TRACE("Read(99.6, 100)");
    {
      // Reqesting [99.6, 199.6) returns [99.5, 100.5).
      auto packet = stage->Read(DefaultCtx(), Fixed(99) + ffl::FromRatio(6, 10), 100);
      ExpectPacket(packet, Fixed(99) + ffl::FromRatio(5, 10), Fixed(100) + ffl::FromRatio(5, 10),
                   payload + 99 * kFormat.bytes_per_frame());
    }
    if (UseCaching()) {
      ExpectAdvanceCalls({Fixed(100) + ffl::FromRatio(5, 10)});
    } else {
      ExpectAdvanceCalls({Fixed(99) + ffl::FromRatio(6, 10), Fixed(100) + ffl::FromRatio(5, 10)});
    }
  }

  {
    SCOPED_TRACE("Read(100.5, 100)");
    {
      auto packet = stage->Read(DefaultCtx(), Fixed(100) + ffl::FromRatio(5, 10), 100);
      ExpectNullPacket(packet);
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
}  // namespace media_audio

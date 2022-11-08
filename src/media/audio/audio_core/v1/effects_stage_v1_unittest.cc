// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/effects_stage_v1.h"

#include <gmock/gmock.h>

#include "src/media/audio/audio_core/shared/process_config.h"
#include "src/media/audio/audio_core/v1/packet_queue.h"
#include "src/media/audio/audio_core/v1/testing/fake_stream.h"
#include "src/media/audio/audio_core/v1/testing/packet_factory.h"
#include "src/media/audio/audio_core/v1/testing/threading_model_fixture.h"
#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/effects_loader/testing/test_effects_v1.h"
#include "src/media/audio/lib/processing/gain.h"

using testing::Each;
using testing::FloatEq;

namespace media::audio {
namespace {

// Used when the ReadLockContext is unused by the test.
static media::audio::ReadableStream::ReadLockContext rlctx;

static const Format k48k2ChanFloatFormat =
    Format::Create(fuchsia::media::AudioStreamType{
                       .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                       .channels = 2,
                       .frames_per_second = 48000,
                   })
        .take_value();

class EffectsStageV1Test : public testing::ThreadingModelFixture {
 protected:
  // Views the memory at |ptr| as a std::array of |N| elements of |T|. If |offset| is provided, it
  // is the number of |T| sized elements to skip at the beginning of |ptr|.
  //
  // It is entirely up to the caller to ensure that values of |T|, |N|, and |offset| are chosen to
  // not overflow |ptr|.
  template <typename T, size_t N>
  std::array<T, N>& as_array(void* ptr, size_t offset = 0) {
    return reinterpret_cast<std::array<T, N>&>(static_cast<T*>(ptr)[offset]);
  }

  struct Options {
    // Position of the first non-zero source frame.
    Fixed first_source_frame;
    // Effect options.
    std::optional<int64_t> block_size;
    std::optional<int64_t> max_frames_per_buffer;
  };
  std::shared_ptr<EffectsStageV1> CreateWithAddOneEffect(Options options);

  testing::PacketFactory& packet_factory() { return packet_factory_; }
  testing::TestEffectsV1Module& test_effects() { return test_effects_; }
  VolumeCurve& volume_curve() { return volume_curve_; }

 private:
  testing::PacketFactory packet_factory_{dispatcher(), k48k2ChanFloatFormat,
                                         zx_system_get_page_size()};
  testing::TestEffectsV1Module test_effects_ = testing::TestEffectsV1Module::Open();
  VolumeCurve volume_curve_ = VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume);
};

std::shared_ptr<EffectsStageV1> EffectsStageV1Test::CreateWithAddOneEffect(Options options) {
  // Create a packet queue to use as our source stream.
  auto timeline_function =
      fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(TimelineRate(
          Fixed(k48k2ChanFloatFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));

  auto stream = std::make_shared<PacketQueue>(
      k48k2ChanFloatFormat, timeline_function,
      context().clock_factory()->CreateClientFixed(clock::AdjustableCloneOfMonotonic()));

  // Create an effect we can load.
  auto e = test_effects().AddEffect("add_1.0");
  e.WithAction(TEST_EFFECTS_ACTION_ADD, 1.0);
  if (options.block_size) {
    e.WithBlockSize(*options.block_size);
  }
  if (options.max_frames_per_buffer) {
    e.WithMaxFramesPerBuffer(*options.max_frames_per_buffer);
  }
  EXPECT_EQ(e.Build(), ZX_OK);

  // Create the effects stage.
  std::vector<PipelineConfig::EffectV1> effects;
  effects.push_back(PipelineConfig::EffectV1{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "add_1.0",
      .effect_config = "",
  });
  auto effects_stage = EffectsStageV1::Create(effects, stream, volume_curve());

  // Enqueue 10ms of frames in the packet queue. All samples are 1.0.
  packet_factory().SeekToFrame(options.first_source_frame);
  stream->PushPacket(packet_factory().CreatePacket(1.0, zx::msec(10)));

  return effects_stage;
}

TEST_F(EffectsStageV1Test, ApplyEffects) {
  auto effects_stage = CreateWithAddOneEffect({});

  {
    // Read the first half of the first packet.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(0), 240);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->start().Floor(), 0);
    EXPECT_EQ(buf->length(), 240);

    // Our effect adds 1.0, so the payload should contain all 2.0s.
    auto& arr = as_array<float, 240>(buf->payload());
    EXPECT_THAT(arr, Each(FloatEq(2.0f)));
  }

  {
    // Read the second half of the first packet.
    // The fractional dest_frame should be floor'd to 240.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(240) + ffl::FromRatio(1, 2), 240);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->start().Floor(), 240);
    EXPECT_EQ(buf->length(), 240);

    // Our effect adds 1.0, so the payload should contain all 2.0s.
    auto& arr = as_array<float, 240>(buf->payload());
    EXPECT_THAT(arr, Each(FloatEq(2.0f)));
  }

  {
    // Read again. This should be null, because there are no more packets.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(480), 480);
    ASSERT_FALSE(buf);
  }
}

TEST_F(EffectsStageV1Test, ApplyEffectsWithOffsetSourcePosition) {
  auto effects_stage = CreateWithAddOneEffect({
      .first_source_frame = Fixed(240),
      .block_size = 480,
      .max_frames_per_buffer = 480,
  });

  {
    // Read the packet.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(0), 480);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->start().Floor(), 0);
    EXPECT_EQ(buf->length(), 480);

    // The source is empty (silent) for the first 240 frames, then all 1.0s
    // for the next 240 frames. Since the block size is 480 frames, these
    // should be processed in one block. Therefore we start with 240 1.0s
    // (0.0+1.0) followed by 240 2.0s (1.0+1.1).
    auto& arr1 = as_array<float, 240>(buf->payload(), 0);
    auto& arr2 = as_array<float, 240>(buf->payload(), 240 * effects_stage->format().channels());
    EXPECT_THAT(arr1, Each(FloatEq(1.0f)));
    EXPECT_THAT(arr2, Each(FloatEq(2.0f)));
  }
}

TEST_F(EffectsStageV1Test, ApplyEffectsWithFractionalSourcePosition) {
  auto effects_stage = CreateWithAddOneEffect({
      .first_source_frame = Fixed(100) + ffl::FromRatio(1, 2),
  });

  // The first source frame is 100.5, which is sampled at dest frame 101.
  const int64_t dest_offset = 101;

  {
    // Read the first half of the first packet.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(dest_offset), 240);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->start().Floor(), dest_offset);
    EXPECT_EQ(buf->start().Fraction().raw_value(), 0);
    EXPECT_EQ(buf->length(), 240);

    // Our effect adds 1.0, so the payload should contain all 2.0s.
    auto& arr = as_array<float, 240>(buf->payload());
    EXPECT_THAT(arr, Each(FloatEq(2.0f)));
  }

  {
    // Read the second half of the first packet.
    // The fractional dest_frame should be floor'd to dest_offset + 240.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(dest_offset + 240) + ffl::FromRatio(1, 2), 240);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->start().Floor(), dest_offset + 240);
    EXPECT_EQ(buf->length(), 240);

    // Our effect adds 1.0, so the payload should contain all 2.0s.
    auto& arr = as_array<float, 240>(buf->payload());
    EXPECT_THAT(arr, Each(FloatEq(2.0f)));
  }

  {
    // Read again. This should be null, because there are no more packets.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(dest_offset + 480), 480);
    ASSERT_FALSE(buf);
  }
}

TEST_F(EffectsStageV1Test, ApplyEffectsReadLockLargerThanProcessingBuffer) {
  auto effects_stage = CreateWithAddOneEffect({
      .first_source_frame = Fixed(240),
      .max_frames_per_buffer = 240,
  });

  {
    // Try to read the first 480ms. The source data does not start until 240ms, so this
    // should return a buffer covering [240ms,480ms).
    auto buf = effects_stage->ReadLock(rlctx, Fixed(0), 480);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->start().Floor(), 240);
    EXPECT_EQ(buf->length(), 240);

    // Our effect adds 1.0, so the payload should contain all 2.0s.
    auto& arr = as_array<float, 240>(buf->payload());
    EXPECT_THAT(arr, Each(FloatEq(2.0f)));
  }

  {
    // Read again where we left off. This should read the remaining 240ms.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(480), 480);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->start().Floor(), 480);
    EXPECT_EQ(buf->length(), 240);

    // Our effect adds 1.0, so the payload should contain all 2.0s.
    auto& arr = as_array<float, 240>(buf->payload());
    EXPECT_THAT(arr, Each(FloatEq(2.0f)));
  }

  {
    // Read again where we left off. This should be null, because there are no more packets.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(720), 480);
    ASSERT_FALSE(buf);
  }
}

TEST_F(EffectsStageV1Test, ApplyEffectsReadLockSmallerThanProcessingBuffer) {
  auto effects_stage = CreateWithAddOneEffect({
      .first_source_frame = Fixed(0),
      .block_size = 720,
      .max_frames_per_buffer = 720,
  });

  {
    // Read the first packet.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(0), 480);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->start().Floor(), 0);
    EXPECT_EQ(buf->length(), 480);

    // Our effect adds 1.0, so the payload should contain all 2.0s.
    auto& arr = as_array<float, 480>(buf->payload());
    EXPECT_THAT(arr, Each(FloatEq(2.0f)));
  }

  {
    // At the second packet, we've already cached "silence" from the source
    // for the first 240 frames.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(480), 480);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->start().Floor(), 480);
    EXPECT_EQ(buf->length(), 240);

    // Our effect adds 1.0, and the source is silent, so the payload should contain all 1.0s.
    auto& arr = as_array<float, 240>(buf->payload());
    EXPECT_THAT(arr, Each(FloatEq(1.0f)));
  }

  {
    // Read again where we left off. This should be null, because our cache is exhausted
    // and the source has no more data.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(720), 480);
    ASSERT_FALSE(buf);
  }
}

TEST_F(EffectsStageV1Test, ApplyEffectsReadLockSmallerThanProcessingBufferWithSourceOffset) {
  auto effects_stage = CreateWithAddOneEffect({
      .first_source_frame = Fixed(720),
      .block_size = 720,
      .max_frames_per_buffer = 720,
  });

  {
    // This ReadLock will attempt read 720 frames from the source, but the source is empty.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(0), 480);
    ASSERT_FALSE(buf);
  }

  {
    // This ReadLock should not read anything from the source because we know
    // from the prior ReadLock that the source is empty until 720.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(480), 240);
    ASSERT_FALSE(buf);
  }

  {
    // Now we have data.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(720), 480);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->start().Floor(), 720);
    EXPECT_EQ(buf->length(), 480);

    // Our effect adds 1.0, so the payload should contain all 2.0s.
    auto& arr = as_array<float, 480>(buf->payload());
    EXPECT_THAT(arr, Each(FloatEq(2.0f)));
  }

  {
    // Source data ends at 720+480=1200, however the last ReadLock processed 240 additional
    // silent frames from the source.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(1200), 480);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->start().Floor(), 1200);
    EXPECT_EQ(buf->length(), 240);

    // Our effect adds 1.0, and the source range is silent, so the payload should contain all 1.0s.
    auto& arr = as_array<float, 240>(buf->payload());
    EXPECT_THAT(arr, Each(FloatEq(1.0f)));
  }

  {
    // Read again where we left off. This should be null, because our cache is exhausted
    // and the source has no more data.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(1440), 480);
    ASSERT_FALSE(buf);
  }
}

TEST_F(EffectsStageV1Test, RespectBlockSize) {
  // Create a source stream.
  auto stream =
      std::make_shared<testing::FakeStream>(k48k2ChanFloatFormat, context().clock_factory());

  // Create an effect we can load.
  const uint32_t kBlockSize = 128;
  test_effects()
      .AddEffect("add_1.0")
      .WithAction(TEST_EFFECTS_ACTION_ADD, 1.0)
      .WithBlockSize(kBlockSize);

  // Create the effects stage.
  std::vector<PipelineConfig::EffectV1> effects;
  effects.push_back(PipelineConfig::EffectV1{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "add_1.0",
      .effect_config = "",
  });
  auto effects_stage = EffectsStageV1::Create(effects, stream, volume_curve());

  EXPECT_EQ(effects_stage->block_size(), kBlockSize);

  // EffectsStage must operate on blocks of 128 at time. Request more than 128 frames.
  // Internally, we should read 2 blocks from the source, process those blocks, then
  // return the first 138 frames. If don't process exactly 256 blocks, the TestEffect
  // processor will fail.
  {
    auto buffer = effects_stage->ReadLock(rlctx, Fixed(0), 138);
    EXPECT_EQ(buffer->start().Floor(), 0);
    ASSERT_EQ(buffer->length(), 138);
    EXPECT_EQ(reinterpret_cast<float*>(buffer->payload())[0], 1.0);
    memset(buffer->payload(), 0, kBlockSize * k48k2ChanFloatFormat.bytes_per_frame());
  }

  // Ask for the second and third blocks. The rest of the second block is immediately available.
  {
    auto buffer = effects_stage->ReadLock(rlctx, Fixed(138), 2 * kBlockSize);
    EXPECT_EQ(buffer->start().Floor(), 138);
    ASSERT_EQ(buffer->length(), 2 * kBlockSize - 138);
    EXPECT_EQ(reinterpret_cast<float*>(buffer->payload())[0], 1.0);
  }

  // Ask for the third block.
  {
    auto buffer = effects_stage->ReadLock(rlctx, Fixed(2 * kBlockSize), kBlockSize);
    EXPECT_EQ(buffer->start().Floor(), 2 * kBlockSize);
    ASSERT_EQ(buffer->length(), kBlockSize);
    EXPECT_EQ(reinterpret_cast<float*>(buffer->payload())[0], 1.0);
  }
}

TEST_F(EffectsStageV1Test, TruncateToMaxBufferSize) {
  // Create a source stream.
  auto stream =
      std::make_shared<testing::FakeStream>(k48k2ChanFloatFormat, context().clock_factory());

  const uint32_t kBlockSize = 128;
  const uint32_t kMaxBufferSize = 300;
  test_effects()
      .AddEffect("add_1.0")
      .WithAction(TEST_EFFECTS_ACTION_ADD, 1.0)
      .WithBlockSize(kBlockSize)
      .WithMaxFramesPerBuffer(kMaxBufferSize);

  // Create the effects stage.
  std::vector<PipelineConfig::EffectV1> effects;
  effects.push_back(PipelineConfig::EffectV1{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "add_1.0",
      .effect_config = "",
  });
  auto effects_stage = EffectsStageV1::Create(effects, stream, volume_curve());

  EXPECT_EQ(effects_stage->block_size(), kBlockSize);

  // Request 4 blocks, but get just 2, because the max buffer size is 300.
  {
    auto buffer = effects_stage->ReadLock(rlctx, Fixed(0), 512);
    EXPECT_EQ(buffer->start().Floor(), 0);
    ASSERT_EQ(buffer->length(), 256);
    EXPECT_EQ(reinterpret_cast<float*>(buffer->payload())[0], 1.0);
    memset(buffer->payload(), 0, kBlockSize * k48k2ChanFloatFormat.bytes_per_frame());
  }
}

TEST_F(EffectsStageV1Test, CompensateForEffectDelayInStreamTimeline) {
  auto stream =
      std::make_shared<testing::FakeStream>(k48k2ChanFloatFormat, context().clock_factory());

  // Setup the timeline function so that time 0 aligns to frame 0 with a rate corresponding to the
  // streams format.
  stream->timeline_function()->Update(TimelineFunction(TimelineRate(
      Fixed(k48k2ChanFloatFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));

  test_effects().AddEffect("effect_with_delay_3").WithSignalLatencyFrames(3);
  test_effects().AddEffect("effect_with_delay_10").WithSignalLatencyFrames(10);

  // Create the effects stage. We expect 13 total frames of latency (summed across the 2 effects).
  std::vector<PipelineConfig::EffectV1> effects;
  effects.push_back(PipelineConfig::EffectV1{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "effect_with_delay_10",
      .effect_config = "",
  });
  effects.push_back(PipelineConfig::EffectV1{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "effect_with_delay_3",
      .effect_config = "",
  });
  auto effects_stage = EffectsStageV1::Create(effects, stream, volume_curve());

  // Since our effect introduces 13 frames of latency, the incoming source frame at time 0 can only
  // emerge from the effect in output frame 13.
  // Conversely, output frame 0 was produced based on the source frame at time -13.
  auto ref_clock_to_output_frac_frame =
      effects_stage->ref_time_to_frac_presentation_frame().timeline_function;
  EXPECT_EQ(Fixed::FromRaw(ref_clock_to_output_frac_frame.Apply(0)), Fixed(13));

  // Similarly, at the time we produce output frame 0, we had to draw upon the source frame from
  // time -13. Use a fuzzy compare to allow for slight rounding errors.
  int64_t frame_13_time = (zx::sec(-13).to_nsecs()) / k48k2ChanFloatFormat.frames_per_second();
  auto frame_13_frac_frames =
      Fixed::FromRaw(ref_clock_to_output_frac_frame.Apply(frame_13_time)).Absolute();
  EXPECT_LE(frame_13_frac_frames.raw_value(), 1);
}

TEST_F(EffectsStageV1Test, AddDelayFramesIntoMinLeadTime) {
  auto stream =
      std::make_shared<testing::FakeStream>(k48k2ChanFloatFormat, context().clock_factory());

  // Setup the timeline function so that time 0 aligns to frame 0 with a rate corresponding to the
  // streams format.
  stream->timeline_function()->Update(TimelineFunction(TimelineRate(
      Fixed(k48k2ChanFloatFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));

  test_effects().AddEffect("effect_with_delay_3").WithSignalLatencyFrames(3);
  test_effects().AddEffect("effect_with_delay_10").WithSignalLatencyFrames(10);

  // Create the effects stage. We expect 13 total frames of latency (summed across the 2 effects).
  std::vector<PipelineConfig::EffectV1> effects;
  effects.push_back(PipelineConfig::EffectV1{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "effect_with_delay_10",
      .effect_config = "",
  });
  effects.push_back(PipelineConfig::EffectV1{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "effect_with_delay_3",
      .effect_config = "",
  });
  auto effects_stage = EffectsStageV1::Create(effects, stream, volume_curve());

  // Check our initial lead time is only the effect delay.
  auto effect_lead_time =
      zx::duration(zx::sec(13).to_nsecs() / k48k2ChanFloatFormat.frames_per_second());
  EXPECT_EQ(effect_lead_time, effects_stage->GetPresentationDelay());

  // Check that setting an external min lead time includes our internal lead time.
  const auto external_lead_time = zx::usec(100);
  effects_stage->SetPresentationDelay(external_lead_time);
  EXPECT_EQ(effect_lead_time + external_lead_time, effects_stage->GetPresentationDelay());
}

static const std::string kInstanceName = "instance_name";
static const std::string kInitialConfig = "different size than kConfig";
static const std::string kConfig = "config";

TEST_F(EffectsStageV1Test, UpdateEffect) {
  // Create a packet queue to use as our source stream.
  auto timeline_function =
      fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(TimelineRate(
          Fixed(k48k2ChanFloatFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));

  auto stream = std::make_shared<PacketQueue>(
      k48k2ChanFloatFormat, timeline_function,
      context().clock_factory()->CreateClientFixed(clock::AdjustableCloneOfMonotonic()));

  // Create an effect we can load.
  test_effects()
      .AddEffect("assign_config_size")
      .WithAction(TEST_EFFECTS_ACTION_ASSIGN_CONFIG_SIZE, 0.0);

  // Create the effects stage.
  std::vector<PipelineConfig::EffectV1> effects;
  effects.push_back(PipelineConfig::EffectV1{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "assign_config_size",
      .instance_name = kInstanceName,
      .effect_config = kInitialConfig,
  });
  auto effects_stage = EffectsStageV1::Create(effects, stream, volume_curve());

  effects_stage->UpdateEffect(kInstanceName, kConfig);

  // Enqueue 10ms of frames in the packet queue.
  stream->PushPacket(packet_factory().CreatePacket(1.0, zx::msec(10)));

  // Read from the effects stage. Our effect sets each sample to the size of the config.
  auto buf = effects_stage->ReadLock(rlctx, Fixed(0), 480);
  ASSERT_TRUE(buf);
  ASSERT_EQ(0u, buf->start().Floor());
  ASSERT_EQ(480u, buf->length());

  float expected_sample = static_cast<float>(kConfig.size());

  auto& arr = as_array<float, 480>(buf->payload());
  EXPECT_THAT(arr, Each(FloatEq(expected_sample)));
}

TEST_F(EffectsStageV1Test, CreateStageWithRechannelization) {
  test_effects()
      .AddEffect("increment")
      .WithChannelization(FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY, FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY)
      .WithAction(TEST_EFFECTS_ACTION_ADD, 1.0);

  // Create a packet queue to use as our source stream.
  auto timeline_function =
      fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(TimelineRate(
          Fixed(k48k2ChanFloatFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));
  auto stream = std::make_shared<PacketQueue>(
      k48k2ChanFloatFormat, timeline_function,
      context().clock_factory()->CreateClientFixed(clock::AdjustableCloneOfMonotonic()));

  // Create the effects stage.
  //
  // We have a source stream that provides 2 channel frames. We'll pass that through one effect that
  // will perform a 2 -> 4 channel upsample. For the existing channels it will increment each sample
  // and for the 'new' channels, it will populate 0's. The second effect will be a simple increment
  // on all 4 channels.
  std::vector<PipelineConfig::EffectV1> effects;
  effects.push_back(PipelineConfig::EffectV1{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "increment",
      .instance_name = "incremement_with_upchannel",
      .effect_config = "",
      .output_channels = 4,
  });
  effects.push_back(PipelineConfig::EffectV1{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "increment",
      .instance_name = "incremement_without_upchannel",
      .effect_config = "",
  });
  auto effects_stage = EffectsStageV1::Create(effects, stream, volume_curve());

  // Enqueue 10ms of frames in the packet queue. All samples will be initialized to 1.0.
  stream->PushPacket(packet_factory().CreatePacket(1.0, zx::msec(10)));
  EXPECT_EQ(4, effects_stage->format().channels());

  {
    // Read from the effects stage. Since our effect adds 1.0 to each sample, and we populated the
    // packet with 1.0 samples, we expect to see only 2.0 samples in the result.
    auto buf = effects_stage->ReadLock(rlctx, Fixed(0), 480);
    ASSERT_TRUE(buf);
    EXPECT_EQ(0, buf->start().Floor());
    EXPECT_EQ(480, buf->length());

    // Expect 480, 4-channel frames.
    auto& arr = as_array<float, 480 * 4>(buf->payload());
    for (size_t i = 0; i < 480; ++i) {
      // The first effect will increment channels 0,1, and upchannel by adding channels 2,3
      // initialized as 0's. The second effect will increment all channels, so channels 0,1 will be
      // incremented twice and channels 2,3 will be incremented once. So we expect each frame to be
      // the samples [3.0, 3.0, 1.0, 1.0].
      ASSERT_FLOAT_EQ(arr[i * 4 + 0], 3.0f) << i;
      ASSERT_FLOAT_EQ(arr[i * 4 + 1], 3.0f) << i;
      ASSERT_FLOAT_EQ(arr[i * 4 + 2], 1.0f) << i;
      ASSERT_FLOAT_EQ(arr[i * 4 + 3], 1.0f) << i;
    }
  }
}

TEST_F(EffectsStageV1Test, SendStreamInfoToEffects) {
  test_effects().AddEffect("increment").WithAction(TEST_EFFECTS_ACTION_ADD, 1.0);

  // Set timeline rate to match our format.
  auto timeline_function = TimelineFunction(TimelineRate(
      Fixed(k48k2ChanFloatFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs()));

  auto input = std::make_shared<testing::FakeStream>(
      k48k2ChanFloatFormat, context().clock_factory(), zx_system_get_page_size());
  input->timeline_function()->Update(timeline_function);

  // Create a simple effects stage.
  std::vector<PipelineConfig::EffectV1> effects;
  effects.push_back(PipelineConfig::EffectV1{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "increment",
      .instance_name = "",
      .effect_config = "",
  });
  auto effects_stage = EffectsStageV1::Create(effects, input, volume_curve());

  constexpr uint32_t kRequestedFrames = 48;

  // Read a buffer with no usages, unity gain.
  int64_t first_frame = 0;
  {
    auto buf = effects_stage->ReadLock(rlctx, Fixed(first_frame), kRequestedFrames);
    ASSERT_TRUE(buf);
    EXPECT_TRUE(buf->usage_mask().is_empty());
    EXPECT_FLOAT_EQ(buf->total_applied_gain_db(), media_audio::kUnityGainDb);
    test_effects_v1_inspect_state effect_state;
    EXPECT_EQ(ZX_OK, test_effects().InspectInstance(
                         effects_stage->effects_processor().GetEffectAt(0).get(), &effect_state));
    EXPECT_EQ(0u, effect_state.stream_info.usage_mask);
    EXPECT_FLOAT_EQ(0.0, effect_state.stream_info.gain_dbfs);
    first_frame = buf->end().Floor();
  }

  // Update our input with some usages and gain.
  input->set_gain_db(-20.0);
  input->set_usage_mask(
      StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::COMMUNICATION)}));
  {
    auto buf = effects_stage->ReadLock(rlctx, Fixed(first_frame), kRequestedFrames);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->usage_mask(),
              StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::COMMUNICATION)}));
    EXPECT_FLOAT_EQ(buf->total_applied_gain_db(), -20.0);
    test_effects_v1_inspect_state effect_state;
    EXPECT_EQ(ZX_OK, test_effects().InspectInstance(
                         effects_stage->effects_processor().GetEffectAt(0).get(), &effect_state));
    EXPECT_EQ(FUCHSIA_AUDIO_EFFECTS_USAGE_COMMUNICATION, effect_state.stream_info.usage_mask);
    EXPECT_FLOAT_EQ(-20.0, effect_state.stream_info.gain_dbfs);
    first_frame = buf->end().Floor();
  }

  // Multiple usages in the mask.
  input->set_gain_db(-4.0);
  input->set_usage_mask(StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::MEDIA),
                                         StreamUsage::WithRenderUsage(RenderUsage::INTERRUPTION)}));
  {
    auto buf = effects_stage->ReadLock(rlctx, Fixed(first_frame), kRequestedFrames);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->usage_mask(),
              StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::MEDIA),
                               StreamUsage::WithRenderUsage(RenderUsage::INTERRUPTION)}));
    EXPECT_FLOAT_EQ(buf->total_applied_gain_db(), -4.0);
    test_effects_v1_inspect_state effect_state;
    EXPECT_EQ(ZX_OK, test_effects().InspectInstance(
                         effects_stage->effects_processor().GetEffectAt(0).get(), &effect_state));
    EXPECT_EQ(FUCHSIA_AUDIO_EFFECTS_USAGE_MEDIA | FUCHSIA_AUDIO_EFFECTS_USAGE_INTERRUPTION,
              effect_state.stream_info.usage_mask);
    EXPECT_FLOAT_EQ(-4.0, effect_state.stream_info.gain_dbfs);
    first_frame = buf->end().Floor();
  }
}

TEST_F(EffectsStageV1Test, RingOut) {
  auto timeline_function =
      fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(TimelineRate(
          Fixed(k48k2ChanFloatFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));
  auto stream = std::make_shared<PacketQueue>(
      k48k2ChanFloatFormat, timeline_function,
      context().clock_factory()->CreateClientFixed(clock::AdjustableCloneOfMonotonic()));

  static const uint32_t kBlockSize = 48;
  static const uint32_t kRingOutBlocks = 3;
  static const uint32_t kRingOutFrames = kBlockSize * kRingOutBlocks;
  test_effects()
      .AddEffect("effect")
      .WithRingOutFrames(kRingOutFrames)
      .WithBlockSize(kBlockSize)
      .WithMaxFramesPerBuffer(kBlockSize);

  std::vector<PipelineConfig::EffectV1> effects;
  effects.push_back(PipelineConfig::EffectV1{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "effect",
      .instance_name = "",
      .effect_config = "",
  });
  auto effects_stage = EffectsStageV1::Create(effects, stream, volume_curve());
  EXPECT_EQ(2, effects_stage->format().channels());

  // Add 48 frames to our source.
  stream->PushPacket(packet_factory().CreatePacket(1.0, zx::msec(1)));

  // Read the frames out.
  {
    auto buf = effects_stage->ReadLock(rlctx, Fixed(0), 480);
    ASSERT_TRUE(buf);
    EXPECT_EQ(0, buf->start().Floor());
    EXPECT_EQ(48, buf->length());
  }

  // Now we expect 3 buffers of ringout; Read the first.
  {
    auto buf = effects_stage->ReadLock(rlctx, Fixed(1 * kBlockSize), kBlockSize);
    ASSERT_TRUE(buf);
    EXPECT_EQ(kBlockSize, buf->start().Floor());
    EXPECT_EQ(kBlockSize, buf->length());
  }

  // Now skip the second and try to read the 3rd. This should return more silence.
  // The skipped buffer:
  //     buf = effects_stage->ReadLock(rlctx, Fixed(2 * kBlockSize), kBlockSize);
  {
    auto buf = effects_stage->ReadLock(rlctx, Fixed(3 * kBlockSize), kBlockSize);
    ASSERT_TRUE(buf);
    EXPECT_EQ(3 * kBlockSize, buf->start().Floor());
    EXPECT_EQ(kBlockSize, buf->length());
  }

  // Nothing after the last frame of ringout.
  {
    auto buf = effects_stage->ReadLock(rlctx, Fixed(4 * kBlockSize), kBlockSize);
    ASSERT_FALSE(buf);
  }
}

}  // namespace
}  // namespace media::audio

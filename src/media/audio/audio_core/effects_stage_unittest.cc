// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/effects_stage.h"

#include <gmock/gmock.h>

#include "src/media/audio/audio_core/audio_clock.h"
#include "src/media/audio/audio_core/packet_queue.h"
#include "src/media/audio/audio_core/process_config.h"
#include "src/media/audio/audio_core/testing/fake_stream.h"
#include "src/media/audio/audio_core/testing/packet_factory.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"
#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/effects_loader/testing/test_effects.h"

using testing::Each;
using testing::FloatEq;

namespace media::audio {
namespace {

const Format k48k2ChanFloatFormat =
    Format::Create(fuchsia::media::AudioStreamType{
                       .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                       .channels = 2,
                       .frames_per_second = 48000,
                   })
        .take_value();

class EffectsStageTest : public testing::ThreadingModelFixture {
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

  testing::TestEffectsModule test_effects_ = testing::TestEffectsModule::Open();
  VolumeCurve volume_curve_ = VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume);
};

TEST_F(EffectsStageTest, ApplyEffectsToSourceStream) {
  testing::PacketFactory packet_factory(dispatcher(), k48k2ChanFloatFormat, PAGE_SIZE);

  // Create a packet queue to use as our source stream.
  auto timeline_function =
      fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(TimelineRate(
          Fixed(k48k2ChanFloatFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));

  auto stream = std::make_shared<PacketQueue>(
      k48k2ChanFloatFormat, timeline_function,
      AudioClock::CreateAsCustom(clock::AdjustableCloneOfMonotonic()));

  // Create an effect we can load.
  test_effects_.AddEffect("add_1.0").WithAction(TEST_EFFECTS_ACTION_ADD, 1.0);

  // Create the effects stage.
  std::vector<PipelineConfig::Effect> effects;
  effects.push_back(PipelineConfig::Effect{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "add_1.0",
      .effect_config = "",
  });
  auto effects_stage = EffectsStage::Create(effects, stream, volume_curve_);

  // Enqueue 10ms of frames in the packet queue.
  stream->PushPacket(packet_factory.CreatePacket(1.0, zx::msec(10)));

  {
    // Read from the effects stage. Since our effect adds 1.0 to each sample, and we populated the
    // packet with 1.0 samples, we expect to see only 2.0 samples in the result.
    auto buf = effects_stage->ReadLock(0, 480);
    ASSERT_TRUE(buf);
    ASSERT_EQ(0u, buf->start().Floor());
    ASSERT_EQ(480u, buf->length().Floor());

    auto& arr = as_array<float, 480>(buf->payload());
    EXPECT_THAT(arr, Each(FloatEq(2.0f)));
  }

  {
    // Read again. This should be null, because there are no more packets.
    auto buf = effects_stage->ReadLock(0, 480);
    ASSERT_FALSE(buf);
  }
}

TEST_F(EffectsStageTest, BlockAlignRequests) {
  // Create a source stream.
  auto stream = std::make_shared<testing::FakeStream>(k48k2ChanFloatFormat);

  // Create an effect we can load.
  const uint32_t kBlockSize = 128;
  test_effects_.AddEffect("add_1.0")
      .WithAction(TEST_EFFECTS_ACTION_ADD, 1.0)
      .WithBlockSize(kBlockSize);

  // Create the effects stage.
  std::vector<PipelineConfig::Effect> effects;
  effects.push_back(PipelineConfig::Effect{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "add_1.0",
      .effect_config = "",
  });
  auto effects_stage = EffectsStage::Create(effects, stream, volume_curve_);

  EXPECT_EQ(effects_stage->block_size(), kBlockSize);

  {
    // Ask for a single negative frame. We should recevie an entire block.
    auto buffer = effects_stage->ReadLock(-1, 1);
    EXPECT_EQ(buffer->start().Floor(), -static_cast<int32_t>(kBlockSize));
    EXPECT_EQ(buffer->length().Floor(), kBlockSize);
  }

  {
    // Ask for 1 frame; expect to get a full block.
    auto buffer = effects_stage->ReadLock(0, 1);
    EXPECT_EQ(buffer->start().Floor(), 0u);
    EXPECT_EQ(buffer->length().Floor(), kBlockSize);
  }

  {
    // Ask for subsequent frames; expect the same block still.
    auto buffer = effects_stage->ReadLock(kBlockSize / 2, kBlockSize / 2);
    EXPECT_EQ(buffer->start().Floor(), 0u);
    EXPECT_EQ(buffer->length().Floor(), kBlockSize);
  }

  {
    // Ask for the second block
    auto buffer = effects_stage->ReadLock(kBlockSize, kBlockSize);
    EXPECT_EQ(buffer->start().Floor(), kBlockSize);
    EXPECT_EQ(buffer->length().Floor(), kBlockSize);
  }

  {
    // Check for a frame to verify we handle frame numbers > UINT32_MAX.
    auto buffer = effects_stage->ReadLock(0x100000000, 1);
    EXPECT_EQ(buffer->start().Floor(), 0x100000000);
    EXPECT_EQ(buffer->length().Floor(), kBlockSize);
  }
}

TEST_F(EffectsStageTest, TruncateToMaxBufferSize) {
  // Create a source stream.
  auto stream = std::make_shared<testing::FakeStream>(k48k2ChanFloatFormat);

  const uint32_t kBlockSize = 128;
  const uint32_t kMaxBufferSize = 300;
  test_effects_.AddEffect("test_effect")
      .WithBlockSize(kBlockSize)
      .WithMaxFramesPerBuffer(kMaxBufferSize);

  // Create the effects stage.
  std::vector<PipelineConfig::Effect> effects;
  effects.push_back(PipelineConfig::Effect{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "test_effect",
      .effect_config = "",
  });
  auto effects_stage = EffectsStage::Create(effects, stream, volume_curve_);

  EXPECT_EQ(effects_stage->block_size(), kBlockSize);

  {
    auto buffer = effects_stage->ReadLock(0, 512);
    EXPECT_EQ(buffer->start().Floor(), 0u);
    // Length is 2 full blocks since 3 blocks would be > 300 frames.
    EXPECT_EQ(buffer->length().Floor(), 256u);
  }
}

TEST_F(EffectsStageTest, CompensateForEffectDelayInStreamTimeline) {
  auto stream = std::make_shared<testing::FakeStream>(k48k2ChanFloatFormat);

  // Setup the timeline function so that time 0 alignes to frame 0 with a rate corresponding to the
  // streams format.
  stream->timeline_function()->Update(TimelineFunction(TimelineRate(
      Fixed(k48k2ChanFloatFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));

  test_effects_.AddEffect("effect_with_delay_3").WithSignalLatencyFrames(3);
  test_effects_.AddEffect("effect_with_delay_10").WithSignalLatencyFrames(10);

  // Create the effects stage. We expect 13 total frames of latency (summed across the 2 effects).
  std::vector<PipelineConfig::Effect> effects;
  effects.push_back(PipelineConfig::Effect{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "effect_with_delay_10",
      .effect_config = "",
  });
  effects.push_back(PipelineConfig::Effect{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "effect_with_delay_3",
      .effect_config = "",
  });
  auto effects_stage = EffectsStage::Create(effects, stream, volume_curve_);

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

TEST_F(EffectsStageTest, AddDelayFramesIntoMinLeadTime) {
  auto stream = std::make_shared<testing::FakeStream>(k48k2ChanFloatFormat);

  // Setup the timeline function so that time 0 alignes to frame 0 with a rate corresponding to the
  // streams format.
  stream->timeline_function()->Update(TimelineFunction(TimelineRate(
      Fixed(k48k2ChanFloatFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));

  test_effects_.AddEffect("effect_with_delay_3").WithSignalLatencyFrames(3);
  test_effects_.AddEffect("effect_with_delay_10").WithSignalLatencyFrames(10);

  // Create the effects stage. We expect 13 total frames of latency (summed across the 2 effects).
  std::vector<PipelineConfig::Effect> effects;
  effects.push_back(PipelineConfig::Effect{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "effect_with_delay_10",
      .effect_config = "",
  });
  effects.push_back(PipelineConfig::Effect{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "effect_with_delay_3",
      .effect_config = "",
  });
  auto effects_stage = EffectsStage::Create(effects, stream, volume_curve_);

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

TEST_F(EffectsStageTest, UpdateEffect) {
  testing::PacketFactory packet_factory(dispatcher(), k48k2ChanFloatFormat, PAGE_SIZE);

  // Create a packet queue to use as our source stream.
  auto timeline_function =
      fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(TimelineRate(
          Fixed(k48k2ChanFloatFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));

  auto stream = std::make_shared<PacketQueue>(
      k48k2ChanFloatFormat, timeline_function,
      AudioClock::CreateAsCustom(clock::AdjustableCloneOfMonotonic()));

  // Create an effect we can load.
  test_effects_.AddEffect("assign_config_size")
      .WithAction(TEST_EFFECTS_ACTION_ASSIGN_CONFIG_SIZE, 0.0);

  // Create the effects stage.
  std::vector<PipelineConfig::Effect> effects;
  effects.push_back(PipelineConfig::Effect{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "assign_config_size",
      .instance_name = kInstanceName,
      .effect_config = kInitialConfig,
  });
  auto effects_stage = EffectsStage::Create(effects, stream, volume_curve_);

  effects_stage->UpdateEffect(kInstanceName, kConfig);

  // Enqueue 10ms of frames in the packet queue.
  stream->PushPacket(packet_factory.CreatePacket(1.0, zx::msec(10)));

  // Read from the effects stage. Our effect sets each sample to the size of the config.
  auto buf = effects_stage->ReadLock(0, 480);
  ASSERT_TRUE(buf);
  ASSERT_EQ(0u, buf->start().Floor());
  ASSERT_EQ(480u, buf->length().Floor());

  float expected_sample = static_cast<float>(kConfig.size());

  auto& arr = as_array<float, 480>(buf->payload());
  EXPECT_THAT(arr, Each(FloatEq(expected_sample)));
}

TEST_F(EffectsStageTest, CreateStageWithRechannelization) {
  test_effects_.AddEffect("increment")
      .WithChannelization(FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY, FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY)
      .WithAction(TEST_EFFECTS_ACTION_ADD, 1.0);

  testing::PacketFactory packet_factory(dispatcher(), k48k2ChanFloatFormat, PAGE_SIZE);

  // Create a packet queue to use as our source stream.
  auto timeline_function =
      fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(TimelineRate(
          Fixed(k48k2ChanFloatFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));
  auto stream = std::make_shared<PacketQueue>(
      k48k2ChanFloatFormat, timeline_function,
      AudioClock::CreateAsCustom(clock::AdjustableCloneOfMonotonic()));

  // Create the effects stage.
  //
  // We have a source stream that provides 2 channel frames. We'll pass that through one effect that
  // will perform a 2 -> 4 channel upsample. For the existing channels it will increment each sample
  // and for the 'new' channels, it will populate 0's. The second effect will be a simple increment
  // on all 4 channels.
  std::vector<PipelineConfig::Effect> effects;
  effects.push_back(PipelineConfig::Effect{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "increment",
      .instance_name = "incremement_with_upchannel",
      .effect_config = "",
      .output_channels = 4,
  });
  effects.push_back(PipelineConfig::Effect{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "increment",
      .instance_name = "incremement_without_upchannel",
      .effect_config = "",
  });
  auto effects_stage = EffectsStage::Create(effects, stream, volume_curve_);

  // Enqueue 10ms of frames in the packet queue. All samples will be initialized to 1.0.
  stream->PushPacket(packet_factory.CreatePacket(1.0, zx::msec(10)));
  EXPECT_EQ(4u, effects_stage->format().channels());

  {
    // Read from the effects stage. Since our effect adds 1.0 to each sample, and we populated the
    // packet with 1.0 samples, we expect to see only 2.0 samples in the result.
    auto buf = effects_stage->ReadLock(0, 480);
    ASSERT_TRUE(buf);
    EXPECT_EQ(0u, buf->start().Floor());
    EXPECT_EQ(480u, buf->length().Floor());

    // Expect 480, 4-channel frames.
    auto& arr = as_array<float, 480 * 4>(buf->payload());
    for (size_t i = 0; i < 480; ++i) {
      // The first effect will increment channels 0,1, and upchannel by adding channels 2,3
      // initialized as 0's. The second effect will increment all channels, so channels 0,1 will be
      // incremented twice and channels 2,3 will be incremented once. So we expect each frame to be
      // the samples [3.0, 3.0, 1.0, 1.0].
      ASSERT_FLOAT_EQ(arr[i * 4 + 0], 3.0f);
      ASSERT_FLOAT_EQ(arr[i * 4 + 1], 3.0f);
      ASSERT_FLOAT_EQ(arr[i * 4 + 2], 1.0f);
      ASSERT_FLOAT_EQ(arr[i * 4 + 3], 1.0f);
    }
  }
}

TEST_F(EffectsStageTest, ReleasePacketWhenFullyConsumed) {
  test_effects_.AddEffect("increment").WithAction(TEST_EFFECTS_ACTION_ADD, 1.0);
  testing::PacketFactory packet_factory(dispatcher(), k48k2ChanFloatFormat, PAGE_SIZE);

  // Create a packet queue to use as our source stream.
  auto timeline_function =
      fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(TimelineRate(
          Fixed(k48k2ChanFloatFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));
  auto stream = std::make_shared<PacketQueue>(
      k48k2ChanFloatFormat, timeline_function,
      AudioClock::CreateAsCustom(clock::AdjustableCloneOfMonotonic()));

  // Create a simple effects stage.
  std::vector<PipelineConfig::Effect> effects;
  effects.push_back(PipelineConfig::Effect{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "increment",
      .instance_name = "",
      .effect_config = "",
  });
  auto effects_stage = EffectsStage::Create(effects, stream, volume_curve_);

  // Enqueue 10ms of frames in the packet queue. All samples will be initialized to 1.0.
  bool packet_released = false;
  stream->PushPacket(packet_factory.CreatePacket(1.0, zx::msec(10),
                                                 [&packet_released] { packet_released = true; }));

  // Acquire a buffer.
  auto buf = effects_stage->ReadLock(0, 480);
  RunLoopUntilIdle();
  ASSERT_TRUE(buf);
  EXPECT_EQ(0u, buf->start().Floor());
  EXPECT_EQ(480u, buf->length().Floor());
  EXPECT_FALSE(packet_released);

  // Now release |buf| and mark it as fully consumed. This should release the underlying packet.
  buf->set_is_fully_consumed(true);
  buf = std::nullopt;
  RunLoopUntilIdle();
  EXPECT_TRUE(packet_released);
}

TEST_F(EffectsStageTest, ReleasePacketWhenNoLongerReferenced) {
  test_effects_.AddEffect("increment").WithAction(TEST_EFFECTS_ACTION_ADD, 1.0);
  testing::PacketFactory packet_factory(dispatcher(), k48k2ChanFloatFormat, PAGE_SIZE);

  // Create a packet queue to use as our source stream.
  auto timeline_function =
      fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(TimelineRate(
          Fixed(k48k2ChanFloatFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));
  auto stream = std::make_shared<PacketQueue>(
      k48k2ChanFloatFormat, timeline_function,
      AudioClock::CreateAsCustom(clock::AdjustableCloneOfMonotonic()));

  // Create a simple effects stage.
  std::vector<PipelineConfig::Effect> effects;
  effects.push_back(PipelineConfig::Effect{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "increment",
      .instance_name = "",
      .effect_config = "",
  });
  auto effects_stage = EffectsStage::Create(effects, stream, volume_curve_);

  // Enqueue 10ms of frames in the packet queue. All samples will be initialized to 1.0.
  bool packet_released = false;
  stream->PushPacket(packet_factory.CreatePacket(1.0, zx::msec(10),
                                                 [&packet_released] { packet_released = true; }));

  // Acquire a buffer.
  auto buf = effects_stage->ReadLock(0, 480);
  RunLoopUntilIdle();
  ASSERT_TRUE(buf);
  EXPECT_EQ(0u, buf->start().Floor());
  EXPECT_EQ(480u, buf->length().Floor());
  EXPECT_FALSE(packet_released);

  // Release |buf|, we don't yet expect the underlying packet to be released.
  buf->set_is_fully_consumed(false);
  buf = std::nullopt;
  RunLoopUntilIdle();
  EXPECT_FALSE(packet_released);

  // Now read another buffer. Since this does not overlap with the last buffer, this should release
  // that packet.
  buf = effects_stage->ReadLock(480, 480);
  RunLoopUntilIdle();
  EXPECT_FALSE(buf);
  EXPECT_TRUE(packet_released);
}

TEST_F(EffectsStageTest, SendStreamInfoToEffects) {
  test_effects_.AddEffect("increment").WithAction(TEST_EFFECTS_ACTION_ADD, 1.0);

  // Set timeline rate to match our format.
  auto timeline_function = TimelineFunction(TimelineRate(
      Fixed(k48k2ChanFloatFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs()));

  auto input = std::make_shared<testing::FakeStream>(k48k2ChanFloatFormat, PAGE_SIZE);
  input->timeline_function()->Update(timeline_function);

  // Create a simple effects stage.
  std::vector<PipelineConfig::Effect> effects;
  effects.push_back(PipelineConfig::Effect{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "increment",
      .instance_name = "",
      .effect_config = "",
  });
  auto effects_stage = EffectsStage::Create(effects, input, volume_curve_);

  constexpr uint32_t kRequestedFrames = 48;

  // Read a buffer with no usages, unity gain.
  int64_t first_frame = 0;
  {
    auto buf = effects_stage->ReadLock(first_frame, kRequestedFrames);
    ASSERT_TRUE(buf);
    EXPECT_TRUE(buf->usage_mask().is_empty());
    EXPECT_FLOAT_EQ(buf->gain_db(), Gain::kUnityGainDb);
    test_effects_inspect_state effect_state;
    EXPECT_EQ(ZX_OK, test_effects_.InspectInstance(
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
    auto buf = effects_stage->ReadLock(first_frame, kRequestedFrames);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->usage_mask(),
              StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::COMMUNICATION)}));
    EXPECT_FLOAT_EQ(buf->gain_db(), -20.0);
    test_effects_inspect_state effect_state;
    EXPECT_EQ(ZX_OK, test_effects_.InspectInstance(
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
    auto buf = effects_stage->ReadLock(first_frame, kRequestedFrames);
    ASSERT_TRUE(buf);
    EXPECT_EQ(buf->usage_mask(),
              StreamUsageMask({StreamUsage::WithRenderUsage(RenderUsage::MEDIA),
                               StreamUsage::WithRenderUsage(RenderUsage::INTERRUPTION)}));
    EXPECT_FLOAT_EQ(buf->gain_db(), -4.0);
    test_effects_inspect_state effect_state;
    EXPECT_EQ(ZX_OK, test_effects_.InspectInstance(
                         effects_stage->effects_processor().GetEffectAt(0).get(), &effect_state));
    EXPECT_EQ(FUCHSIA_AUDIO_EFFECTS_USAGE_MEDIA | FUCHSIA_AUDIO_EFFECTS_USAGE_INTERRUPTION,
              effect_state.stream_info.usage_mask);
    EXPECT_FLOAT_EQ(-4.0, effect_state.stream_info.gain_dbfs);
    first_frame = buf->end().Floor();
  }
}

TEST_F(EffectsStageTest, SkipRingoutIfDiscontinuous) {
  testing::PacketFactory packet_factory{dispatcher(), k48k2ChanFloatFormat, PAGE_SIZE};
  auto timeline_function =
      fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(TimelineRate(
          Fixed(k48k2ChanFloatFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));
  auto stream = std::make_shared<PacketQueue>(
      k48k2ChanFloatFormat, timeline_function,
      AudioClock::CreateAsCustom(clock::AdjustableCloneOfMonotonic()));

  static const uint32_t kBlockSize = 48;
  static const uint32_t kRingOutBlocks = 4;
  static const uint32_t kRingOutFrames = kBlockSize * kRingOutBlocks;
  test_effects_.AddEffect("effect")
      .WithRingOutFrames(kRingOutFrames)
      .WithBlockSize(kBlockSize)
      .WithMaxFramesPerBuffer(kBlockSize);

  std::vector<PipelineConfig::Effect> effects;
  effects.push_back(PipelineConfig::Effect{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "effect",
      .instance_name = "",
      .effect_config = "",
  });
  auto effects_stage = EffectsStage::Create(effects, stream, volume_curve_);
  EXPECT_EQ(2u, effects_stage->format().channels());

  // Add 48 frames to our source.
  stream->PushPacket(packet_factory.CreatePacket(1.0, zx::msec(1)));

  {  // Read the frames out.
    auto buf = effects_stage->ReadLock(0, 480);
    ASSERT_TRUE(buf);
    EXPECT_EQ(0u, buf->start().Floor());
    EXPECT_EQ(48u, buf->length().Floor());
  }

  // Now we expect 3 buffers of ringout; Read the first.
  {
    auto buf = effects_stage->ReadLock(1 * kBlockSize, kBlockSize);
    ASSERT_TRUE(buf);
    EXPECT_EQ(kBlockSize, buf->start().Floor());
    EXPECT_EQ(kBlockSize, buf->length().Floor());
  }

  // Now skip the second and try to read the 3rd. This is discontinuous and should not return any
  // data.
  //
  // The skipped buffer:
  //     buf = effects_stage->ReadLock(2 * kBlockSize, kBlockSize);
  {
    auto buf = effects_stage->ReadLock(3 * kBlockSize, kBlockSize);
    ASSERT_FALSE(buf);
  }

  // Now read the 4th packet. Since we had a previous discontinuous buffer, this is still silent.
  {
    auto buf = effects_stage->ReadLock(4 * kBlockSize, kBlockSize);
    ASSERT_FALSE(buf);
  }
}

struct RingOutTestParameters {
  Format format;
  uint32_t effect_ring_out_frames;
  uint32_t effect_block_size;
  uint32_t effect_max_frames_per_buffer;
  // The expected number of frames in the ring-out buffers.
  uint32_t ring_out_block_frames;
};

class EffectsStageRingoutTest : public EffectsStageTest,
                                public ::testing::WithParamInterface<RingOutTestParameters> {
 protected:
  void SetUp() override {
    auto timeline_function =
        fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(TimelineRate(
            Fixed(k48k2ChanFloatFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs())));
    stream_ = std::make_shared<PacketQueue>(
        k48k2ChanFloatFormat, timeline_function,
        AudioClock::CreateAsCustom(clock::AdjustableCloneOfMonotonic()));
  }

  testing::PacketFactory packet_factory_{dispatcher(), k48k2ChanFloatFormat, PAGE_SIZE};
  std::shared_ptr<PacketQueue> stream_;
};

TEST_P(EffectsStageRingoutTest, RingoutBuffer) {
  auto ringout_buffer = EffectsStage::RingoutBuffer::Create(
      GetParam().format, GetParam().effect_ring_out_frames, GetParam().effect_max_frames_per_buffer,
      GetParam().effect_block_size);

  EXPECT_EQ(GetParam().ring_out_block_frames, ringout_buffer.buffer_frames);
  EXPECT_EQ(GetParam().effect_ring_out_frames, ringout_buffer.total_frames);

  if (GetParam().effect_ring_out_frames) {
    EXPECT_EQ(GetParam().format.channels() * GetParam().ring_out_block_frames,
              ringout_buffer.buffer.size());
  } else {
    EXPECT_EQ(0u, ringout_buffer.buffer.size());
  }

  if (GetParam().effect_block_size && GetParam().ring_out_block_frames) {
    EXPECT_EQ(0u, ringout_buffer.buffer_frames % GetParam().ring_out_block_frames);
  }
}

TEST_P(EffectsStageRingoutTest, RingoutFrames) {
  test_effects_.AddEffect("effect")
      .WithRingOutFrames(GetParam().effect_ring_out_frames)
      .WithBlockSize(GetParam().effect_block_size)
      .WithMaxFramesPerBuffer(GetParam().effect_max_frames_per_buffer);

  std::vector<PipelineConfig::Effect> effects;
  effects.push_back(PipelineConfig::Effect{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "effect",
      .instance_name = "",
      .effect_config = "",
  });
  auto effects_stage = EffectsStage::Create(effects, stream_, volume_curve_);
  EXPECT_EQ(2u, effects_stage->format().channels());

  // Add 48 frames to our source.
  stream_->PushPacket(packet_factory_.CreatePacket(1.0, zx::msec(1)));

  {  // Read the frames out.
    auto buf = effects_stage->ReadLock(0, 480);
    ASSERT_TRUE(buf);
    EXPECT_EQ(0u, buf->start().Floor());
    EXPECT_EQ(48u, buf->length().Floor());
  }

  // Now we expect our ringout to be split across many buffers.
  int64_t start_frame = 48;
  uint32_t ringout_frames = 0;
  {
    while (ringout_frames < GetParam().effect_ring_out_frames) {
      auto buf = effects_stage->ReadLock(start_frame, GetParam().effect_ring_out_frames);
      ASSERT_TRUE(buf);
      EXPECT_EQ(start_frame, buf->start().Floor());
      EXPECT_EQ(GetParam().ring_out_block_frames, buf->length().Floor());
      start_frame += GetParam().ring_out_block_frames;
      ringout_frames += GetParam().ring_out_block_frames;
    }
  }

  {
    auto buf = effects_stage->ReadLock(start_frame, 480);
    EXPECT_FALSE(buf);
  }

  // Add another data packet to verify we correctly reset the ringout when the source goes silent
  // again.
  start_frame += 480;
  packet_factory_.SeekToFrame(start_frame);
  stream_->PushPacket(packet_factory_.CreatePacket(1.0, zx::msec(1)));

  {  // Read the frames out.
    auto buf = effects_stage->ReadLock(start_frame, 48);
    ASSERT_TRUE(buf);
    EXPECT_EQ(start_frame, buf->start().Floor());
    EXPECT_EQ(48u, buf->length().Floor());
    start_frame += buf->length().Floor();
  }

  // Now we expect our ringout to be split across many buffers.
  ringout_frames = 0;
  {
    while (ringout_frames < GetParam().effect_ring_out_frames) {
      auto buf = effects_stage->ReadLock(start_frame, GetParam().effect_ring_out_frames);
      ASSERT_TRUE(buf);
      EXPECT_EQ(start_frame, buf->start().Floor());
      EXPECT_EQ(GetParam().ring_out_block_frames, buf->length().Floor());
      start_frame += GetParam().ring_out_block_frames;
      ringout_frames += GetParam().ring_out_block_frames;
    }
  }

  {
    auto buf = effects_stage->ReadLock(48, 480);
    EXPECT_FALSE(buf);
  }
}

const RingOutTestParameters kNoRingout{
    .format = k48k2ChanFloatFormat,
    .effect_ring_out_frames = 0,
    .effect_block_size = 1,
    .effect_max_frames_per_buffer = FUCHSIA_AUDIO_EFFECTS_FRAMES_PER_BUFFER_ANY,
    .ring_out_block_frames = 0,
};

const RingOutTestParameters kSmallRingOutNoBlockSize{
    .format = k48k2ChanFloatFormat,
    .effect_ring_out_frames = 4,
    .effect_block_size = 1,
    .effect_max_frames_per_buffer = FUCHSIA_AUDIO_EFFECTS_FRAMES_PER_BUFFER_ANY,
    // Should be a single block
    .ring_out_block_frames = 4,
};

const RingOutTestParameters kLargeRingOutNoBlockSize{
    .format = k48k2ChanFloatFormat,
    .effect_ring_out_frames = 8192,
    .effect_block_size = 1,
    .effect_max_frames_per_buffer = FUCHSIA_AUDIO_EFFECTS_FRAMES_PER_BUFFER_ANY,
    // Matches |kTargetRingoutBufferFrames| in effects_stage.cc
    .ring_out_block_frames = 240,
};

const RingOutTestParameters kMaxFramesPerBufferLowerThanRingOutFrames{
    .format = k48k2ChanFloatFormat,
    .effect_ring_out_frames = 8192,
    .effect_block_size = 1,
    .effect_max_frames_per_buffer = 128,
    .ring_out_block_frames = 128,
};

INSTANTIATE_TEST_SUITE_P(EffectsStageRingoutTestInstance, EffectsStageRingoutTest,
                         ::testing::Values(kNoRingout, kSmallRingOutNoBlockSize,
                                           kLargeRingOutNoBlockSize,
                                           kMaxFramesPerBufferLowerThanRingOutFrames));

}  // namespace
}  // namespace media::audio

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/effects_stage.h"

#include <gmock/gmock.h>

#include "src/media/audio/audio_core/packet_queue.h"
#include "src/media/audio/audio_core/process_config.h"
#include "src/media/audio/audio_core/testing/fake_stream.h"
#include "src/media/audio/audio_core/testing/packet_factory.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"
#include "src/media/audio/lib/effects_loader/testing/test_effects.h"

using testing::Each;
using testing::FloatEq;

namespace media::audio {
namespace {

const Format kDefaultFormat =
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
};

TEST_F(EffectsStageTest, ApplyEffectsToSourceStream) {
  testing::PacketFactory packet_factory(dispatcher(), kDefaultFormat, PAGE_SIZE);

  // Create a packet queue to use as our source stream.
  auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
      TimelineRate(FractionalFrames<uint32_t>(kDefaultFormat.frames_per_second()).raw_value(),
                   zx::sec(1).to_nsecs())));
  auto stream = std::make_shared<PacketQueue>(kDefaultFormat, timeline_function);

  // Create an effect we can load.
  test_effects_.AddEffect("add_1.0").WithAction(TEST_EFFECTS_ACTION_ADD, 1.0);

  // Create the effects stage.
  std::vector<PipelineConfig::Effect> effects;
  effects.push_back(PipelineConfig::Effect{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "add_1.0",
      .effect_config = "",
  });
  auto effects_stage = EffectsStage::Create(effects, stream);

  // Enqueue 10ms of frames in the packet queue.
  stream->PushPacket(packet_factory.CreatePacket(1.0, zx::msec(10)));

  {
    // Read from the effects stage. Since our effect adds 1.0 to each sample, and we populated the
    // packet with 1.0 samples, we expect to see only 2.0 samples in the result.
    auto buf = effects_stage->ReadLock(zx::time(0) + zx::msec(10), 0, 480);
    ASSERT_TRUE(buf);
    ASSERT_EQ(0u, buf->start().Floor());
    ASSERT_EQ(480u, buf->length().Floor());

    auto& arr = as_array<float, 480>(buf->payload());
    EXPECT_THAT(arr, Each(FloatEq(2.0f)));
  }

  {
    // Read again. This should be null, because there are no more packets.
    auto buf = effects_stage->ReadLock(zx::time(0) + zx::msec(10), 0, 480);
    ASSERT_FALSE(buf);
  }
}

TEST_F(EffectsStageTest, BlockAlignRequests) {
  // Create a source stream.
  auto stream = std::make_shared<testing::FakeStream>(kDefaultFormat);

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
  auto effects_stage = EffectsStage::Create(effects, stream);

  EXPECT_EQ(effects_stage->block_size(), kBlockSize);

  {
    // Ask for 1 frame; expect to get a full block.
    auto buffer = effects_stage->ReadLock(zx::time(0), 0, 1);
    EXPECT_EQ(buffer->start().Floor(), 0u);
    EXPECT_EQ(buffer->length().Floor(), kBlockSize);
  }

  {
    // Ask for subsequent frames; expect the same block still.
    auto buffer = effects_stage->ReadLock(zx::time(0), kBlockSize / 2, kBlockSize / 2);
    EXPECT_EQ(buffer->start().Floor(), 0u);
    EXPECT_EQ(buffer->length().Floor(), kBlockSize);
  }

  {
    // Ask for the second block
    auto buffer = effects_stage->ReadLock(zx::time(0), kBlockSize, kBlockSize);
    EXPECT_EQ(buffer->start().Floor(), kBlockSize);
    EXPECT_EQ(buffer->length().Floor(), kBlockSize);
  }

  {
    // Check for a frame to verify we handle frame numbers > UINT32_MAX.
    auto buffer = effects_stage->ReadLock(zx::time(0), 0x100000000, 1);
    EXPECT_EQ(buffer->start().Floor(), 0x100000000);
    EXPECT_EQ(buffer->length().Floor(), kBlockSize);
  }
}

TEST_F(EffectsStageTest, TruncateToMaxBufferSize) {
  // Create a source stream.
  auto stream = std::make_shared<testing::FakeStream>(kDefaultFormat);

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
  auto effects_stage = EffectsStage::Create(effects, stream);

  EXPECT_EQ(effects_stage->block_size(), kBlockSize);

  {
    auto buffer = effects_stage->ReadLock(zx::time(0), 0, 512);
    EXPECT_EQ(buffer->start().Floor(), 0u);
    // Length is 2 full blocks since 3 blocks would be > 300 frames.
    EXPECT_EQ(buffer->length().Floor(), 256u);
  }
}

TEST_F(EffectsStageTest, CompensateForEffectDelayInStreamTimeline) {
  auto stream = std::make_shared<testing::FakeStream>(kDefaultFormat);

  // Setup the timeline function so that time 0 alignes to frame 0 with a rate corresponding to the
  // streams format.
  stream->timeline_function()->Update(TimelineFunction(
      TimelineRate(FractionalFrames<int64_t>(kDefaultFormat.frames_per_second()).raw_value(),
                   zx::sec(1).to_nsecs())));

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
  auto effects_stage = EffectsStage::Create(effects, stream);

  // Since our effect introduces 13 frames of latency, the incoming source frame at time 0 can only
  // emerge from the effect in output frame 13.
  // Conversely, output frame 0 was produced based on the source frame at time -13.
  auto ref_clock_to_output_frac_frame =
      effects_stage->ReferenceClockToFractionalFrames().timeline_function;
  EXPECT_EQ(FractionalFrames<int64_t>::FromRaw(ref_clock_to_output_frac_frame.Apply(0)),
            FractionalFrames<int64_t>(13));

  // Similarly, at the time we produce output frame 0, we had to draw upon the source frame from
  // time -13. Use a fuzzy compare to allow for slight rounding errors.
  int64_t frame_13_time = (zx::sec(-13).to_nsecs()) / kDefaultFormat.frames_per_second();
  auto frame_13_frac_frames =
      FractionalFrames<int64_t>::FromRaw(ref_clock_to_output_frac_frame.Apply(frame_13_time))
          .Absolute();
  EXPECT_LE(frame_13_frac_frames.raw_value(), 1);
}

TEST_F(EffectsStageTest, AddDelayFramesIntoMinLeadTime) {
  auto stream = std::make_shared<testing::FakeStream>(kDefaultFormat);

  // Setup the timeline function so that time 0 alignes to frame 0 with a rate corresponding to the
  // streams format.
  stream->timeline_function()->Update(TimelineFunction(
      TimelineRate(FractionalFrames<int64_t>(kDefaultFormat.frames_per_second()).raw_value(),
                   zx::sec(1).to_nsecs())));

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
  auto effects_stage = EffectsStage::Create(effects, stream);

  // Check our initial lead time is only the effect delay.
  auto effect_lead_time = zx::duration(zx::sec(13).to_nsecs() / kDefaultFormat.frames_per_second());
  EXPECT_EQ(effect_lead_time, effects_stage->GetMinLeadTime());

  // Check that setting an external min lead time includes our internal lead time.
  const auto external_lead_time = zx::usec(100);
  effects_stage->SetMinLeadTime(external_lead_time);
  EXPECT_EQ(effect_lead_time + external_lead_time, effects_stage->GetMinLeadTime());
}

static const std::string kInstanceName = "instance_name";
static const std::string kInitialConfig = "different size than kConfig";
static const std::string kConfig = "config";

TEST_F(EffectsStageTest, UpdateEffect) {
  testing::PacketFactory packet_factory(dispatcher(), kDefaultFormat, PAGE_SIZE);

  // Create a packet queue to use as our source stream.
  auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
      TimelineRate(FractionalFrames<uint32_t>(kDefaultFormat.frames_per_second()).raw_value(),
                   zx::sec(1).to_nsecs())));
  auto stream = std::make_shared<PacketQueue>(kDefaultFormat, timeline_function);

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
  auto effects_stage = EffectsStage::Create(effects, stream);

  effects_stage->UpdateEffect(kInstanceName, kConfig);

  // Enqueue 10ms of frames in the packet queue.
  stream->PushPacket(packet_factory.CreatePacket(1.0, zx::msec(10)));

  // Read from the effects stage. Our effect sets each sample to the size of the config.
  auto buf = effects_stage->ReadLock(zx::time(0) + zx::msec(10), 0, 480);
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

  testing::PacketFactory packet_factory(dispatcher(), kDefaultFormat, PAGE_SIZE);

  // Create a packet queue to use as our source stream.
  auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
      TimelineRate(FractionalFrames<uint32_t>(kDefaultFormat.frames_per_second()).raw_value(),
                   zx::sec(1).to_nsecs())));
  auto stream = std::make_shared<PacketQueue>(kDefaultFormat, timeline_function);

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
  auto effects_stage = EffectsStage::Create(effects, stream);

  // Enqueue 10ms of frames in the packet queue. All samples will be initialized to 1.0.
  stream->PushPacket(packet_factory.CreatePacket(1.0, zx::msec(10)));
  EXPECT_EQ(4u, effects_stage->format().channels());

  {
    // Read from the effects stage. Since our effect adds 1.0 to each sample, and we populated the
    // packet with 1.0 samples, we expect to see only 2.0 samples in the result.
    auto buf = effects_stage->ReadLock(zx::time(0) + zx::msec(10), 0, 480);
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
  testing::PacketFactory packet_factory(dispatcher(), kDefaultFormat, PAGE_SIZE);

  // Create a packet queue to use as our source stream.
  auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
      TimelineRate(FractionalFrames<uint32_t>(kDefaultFormat.frames_per_second()).raw_value(),
                   zx::sec(1).to_nsecs())));
  auto stream = std::make_shared<PacketQueue>(kDefaultFormat, timeline_function);

  // Create a simple effects stage.
  std::vector<PipelineConfig::Effect> effects;
  effects.push_back(PipelineConfig::Effect{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "increment",
      .instance_name = "",
      .effect_config = "",
  });
  auto effects_stage = EffectsStage::Create(effects, stream);

  // Enqueue 10ms of frames in the packet queue. All samples will be initialized to 1.0.
  bool packet_released = false;
  stream->PushPacket(packet_factory.CreatePacket(1.0, zx::msec(10),
                                                 [&packet_released] { packet_released = true; }));

  // Acquire a buffer.
  auto buf = effects_stage->ReadLock(zx::time(0) + zx::msec(10), 0, 480);
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
  testing::PacketFactory packet_factory(dispatcher(), kDefaultFormat, PAGE_SIZE);

  // Create a packet queue to use as our source stream.
  auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
      TimelineRate(FractionalFrames<uint32_t>(kDefaultFormat.frames_per_second()).raw_value(),
                   zx::sec(1).to_nsecs())));
  auto stream = std::make_shared<PacketQueue>(kDefaultFormat, timeline_function);

  // Create a simple effects stage.
  std::vector<PipelineConfig::Effect> effects;
  effects.push_back(PipelineConfig::Effect{
      .lib_name = testing::kTestEffectsModuleName,
      .effect_name = "increment",
      .instance_name = "",
      .effect_config = "",
  });
  auto effects_stage = EffectsStage::Create(effects, stream);

  // Enqueue 10ms of frames in the packet queue. All samples will be initialized to 1.0.
  bool packet_released = false;
  stream->PushPacket(packet_factory.CreatePacket(1.0, zx::msec(10),
                                                 [&packet_released] { packet_released = true; }));

  // Acquire a buffer.
  auto buf = effects_stage->ReadLock(zx::time(0) + zx::msec(10), 0, 480);
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
  buf = effects_stage->ReadLock(zx::time(0) + zx::msec(10), 480, 480);
  RunLoopUntilIdle();
  EXPECT_FALSE(buf);
  EXPECT_TRUE(packet_released);
}

}  // namespace
}  // namespace media::audio

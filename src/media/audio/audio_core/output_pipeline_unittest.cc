// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/output_pipeline.h"

#include <gmock/gmock.h>

#include "src/media/audio/audio_core/packet_queue.h"
#include "src/media/audio/audio_core/process_config.h"
#include "src/media/audio/audio_core/testing/packet_factory.h"
#include "src/media/audio/audio_core/testing/threading_model_fixture.h"
#include "src/media/audio/audio_core/usage_settings.h"
#include "src/media/audio/lib/effects_loader/testing/test_effects.h"

using testing::Each;
using testing::Eq;
using testing::Pointwise;

namespace media::audio {
namespace {

const Format kDefaultFormat = Format(fuchsia::media::AudioStreamType{
    .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
    .channels = 2,
    .frames_per_second = 48000,
});
const TimelineFunction kDefaultTransform = TimelineFunction(
    TimelineRate(FractionalFrames<uint32_t>(kDefaultFormat.frames_per_second()).raw_value(),
                 zx::sec(1).to_nsecs()));

class OutputPipelineTest : public testing::ThreadingModelFixture {
 protected:
  std::shared_ptr<OutputPipeline> CreateOutputPipeline() {
    ProcessConfig::Builder builder;
    PipelineConfig::MixGroup root{
        .name = "linearize",
        .input_streams =
            {
                fuchsia::media::AudioRenderUsage::BACKGROUND,
            },
        .effects = {},
        .inputs = {{.name = "mix",
                    .input_streams =
                        {
                            fuchsia::media::AudioRenderUsage::INTERRUPTION,
                        },
                    .effects = {},
                    .inputs = {{
                                   .name = "default",
                                   .input_streams =
                                       {
                                           fuchsia::media::AudioRenderUsage::MEDIA,
                                           fuchsia::media::AudioRenderUsage::SYSTEM_AGENT,
                                       },
                                   .effects = {},
                               },
                               {
                                   .name = "communications",
                                   .input_streams =
                                       {
                                           fuchsia::media::AudioRenderUsage::COMMUNICATION,
                                       },
                                   .effects = {},
                               }}}}};

    auto pipeline_config = PipelineConfig(root);
    return std::make_shared<OutputPipeline>(pipeline_config, kDefaultFormat, 128,
                                            kDefaultTransform);
  }

  void CheckBuffer(void* buffer, float expected_sample, size_t num_samples) {
    float* floats = reinterpret_cast<float*>(buffer);
    for (size_t i = 0; i < num_samples; ++i) {
      ASSERT_FLOAT_EQ(expected_sample, floats[i]);
    }
  }
};

TEST_F(OutputPipelineTest, Trim) {
  auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(kDefaultTransform);
  auto stream1 = std::make_shared<PacketQueue>(kDefaultFormat, timeline_function);
  auto stream2 = std::make_shared<PacketQueue>(kDefaultFormat, timeline_function);
  auto stream3 = std::make_shared<PacketQueue>(kDefaultFormat, timeline_function);
  auto stream4 = std::make_shared<PacketQueue>(kDefaultFormat, timeline_function);

  // Add some streams so that one is routed to each mix stage in our pipeline.
  auto pipeline = CreateOutputPipeline();
  pipeline->AddInput(stream1, UsageFrom(fuchsia::media::AudioRenderUsage::BACKGROUND));
  pipeline->AddInput(stream2, UsageFrom(fuchsia::media::AudioRenderUsage::INTERRUPTION));
  pipeline->AddInput(stream3, UsageFrom(fuchsia::media::AudioRenderUsage::MEDIA));
  pipeline->AddInput(stream4, UsageFrom(fuchsia::media::AudioRenderUsage::COMMUNICATION));

  bool packet_released[8] = {};
  {
    testing::PacketFactory packet_factory(dispatcher(), kDefaultFormat, PAGE_SIZE);
    stream1->PushPacket(packet_factory.CreatePacket(
        1.0, zx::msec(5), [&packet_released] { packet_released[0] = true; }));
    stream1->PushPacket(packet_factory.CreatePacket(
        1.0, zx::msec(5), [&packet_released] { packet_released[1] = true; }));
  }
  {
    testing::PacketFactory packet_factory(dispatcher(), kDefaultFormat, PAGE_SIZE);
    stream2->PushPacket(packet_factory.CreatePacket(
        1.0, zx::msec(5), [&packet_released] { packet_released[2] = true; }));
    stream2->PushPacket(packet_factory.CreatePacket(
        1.0, zx::msec(5), [&packet_released] { packet_released[3] = true; }));
  }
  {
    testing::PacketFactory packet_factory(dispatcher(), kDefaultFormat, PAGE_SIZE);
    stream3->PushPacket(packet_factory.CreatePacket(
        1.0, zx::msec(5), [&packet_released] { packet_released[4] = true; }));
    stream3->PushPacket(packet_factory.CreatePacket(
        1.0, zx::msec(5), [&packet_released] { packet_released[5] = true; }));
  }
  {
    testing::PacketFactory packet_factory(dispatcher(), kDefaultFormat, PAGE_SIZE);
    stream4->PushPacket(packet_factory.CreatePacket(
        1.0, zx::msec(5), [&packet_released] { packet_released[6] = true; }));
    stream4->PushPacket(packet_factory.CreatePacket(
        1.0, zx::msec(5), [&packet_released] { packet_released[7] = true; }));
  }

  // After 4ms we should still be retaining all packets.
  pipeline->Trim(zx::time(0) + zx::msec(4));
  RunLoopUntilIdle();
  EXPECT_THAT(packet_released, Each(Eq(false)));

  // At 5ms we should have trimmed the first packet from each queue.
  pipeline->Trim(zx::time(0) + zx::msec(5));
  RunLoopUntilIdle();
  EXPECT_THAT(packet_released,
              Pointwise(Eq(), {true, false, true, false, true, false, true, false}));

  // After 10ms we should have trimmed all the packets.
  pipeline->Trim(zx::time(0) + zx::msec(10));
  RunLoopUntilIdle();
  EXPECT_THAT(packet_released, Each(Eq(true)));
}

TEST_F(OutputPipelineTest, Loopback) {
  auto test_effects = testing::OpenTestEffectsExt();
  ASSERT_EQ(ZX_OK, test_effects->add_effect({{"add_1.0", FUCHSIA_AUDIO_EFFECTS_CHANNELS_ANY,
                                              FUCHSIA_AUDIO_EFFECTS_CHANNELS_SAME_AS_IN},
                                             FUCHSIA_AUDIO_EFFECTS_BLOCK_SIZE_ANY,
                                             FUCHSIA_AUDIO_EFFECTS_FRAMES_PER_BUFFER_ANY,
                                             TEST_EFFECTS_ACTION_ADD,
                                             1.0}));
  PipelineConfig::MixGroup root{.name = "linearize",
                                .input_streams =
                                    {
                                        fuchsia::media::AudioRenderUsage::BACKGROUND,
                                    },
                                .effects =
                                    {
                                        {
                                            .lib_name = "test_effects.so",
                                            .effect_name = "add_1.0",
                                            .instance_name = "",
                                            .effect_config = "",
                                        },
                                    },
                                .inputs = {{
                                    .name = "mix",
                                    .input_streams =
                                        {
                                            fuchsia::media::AudioRenderUsage::MEDIA,
                                            fuchsia::media::AudioRenderUsage::SYSTEM_AGENT,
                                            fuchsia::media::AudioRenderUsage::INTERRUPTION,
                                            fuchsia::media::AudioRenderUsage::COMMUNICATION,
                                        },
                                    .effects = {},
                                    .loopback = true,
                                }}};
  auto pipeline_config = PipelineConfig(root);
  auto pipeline =
      std::make_shared<OutputPipeline>(pipeline_config, kDefaultFormat, 128, kDefaultTransform);

  // Verify our stream from the pipeline has the effects applied (we have no input streams so we
  // should have silence with a single effect that adds 1.0 to each sample, so we expect all samples
  // to be 1.0).
  auto buf = pipeline->LockBuffer(zx::time(0) + zx::msec(1), 0, 48);
  ASSERT_TRUE(buf);
  ASSERT_EQ(buf->start().Floor(), 0u);
  ASSERT_EQ(buf->length().Floor(), 48u);
  CheckBuffer(buf->payload(), 1.0, 96);

  // Verify our loopback stream contains no effects, since our loopback point is before the effects
  // have been applied.
  auto loopback_buf = pipeline->loopback()->LockBuffer(zx::time(0) + zx::msec(1), 0, 48);
  ASSERT_TRUE(loopback_buf);
  ASSERT_EQ(loopback_buf->start().Floor(), 0u);
  ASSERT_EQ(loopback_buf->length().Floor(), 48u);
  CheckBuffer(loopback_buf->payload(), 0.0, 96);

  test_effects->clear_effects();
}

}  // namespace
}  // namespace media::audio

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
const TimelineFunction kOneFramePerMs = TimelineFunction(TimelineRate(1, 1'000'000));

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
    auto config = builder
                      .SetDefaultVolumeCurve(
                          VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume))
                      .SetPipeline(PipelineConfig(std::move(root)))
                      .Build();

    return std::make_shared<OutputPipeline>(config.pipeline(), kDefaultFormat, 128, kOneFramePerMs);
  }
};

TEST_F(OutputPipelineTest, Trim) {
  auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(TimelineFunction(
      TimelineRate(FractionalFrames<uint32_t>(kDefaultFormat.frames_per_second()).raw_value(),
                   zx::sec(1).to_nsecs())));
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

}  // namespace
}  // namespace media::audio

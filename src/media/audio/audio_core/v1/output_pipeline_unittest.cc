// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/output_pipeline.h"

#include <gmock/gmock.h>

#include "src/media/audio/audio_core/v1/packet_queue.h"
#include "src/media/audio/audio_core/v1/process_config.h"
#include "src/media/audio/audio_core/v1/testing/fake_stream.h"
#include "src/media/audio/audio_core/v1/testing/packet_factory.h"
#include "src/media/audio/audio_core/v1/testing/threading_model_fixture.h"
#include "src/media/audio/audio_core/v1/usage_settings.h"
#include "src/media/audio/effects/test_effects/test_effects_v2.h"
#include "src/media/audio/lib/clock/clone_mono.h"
#include "src/media/audio/lib/clock/testing/clock_test.h"
#include "src/media/audio/lib/clock/utils.h"
#include "src/media/audio/lib/effects_loader/testing/test_effects_v1.h"

using testing::Each;
using testing::Eq;
using testing::Pointwise;

namespace media::audio {
namespace {

// Used when the ReadLockContext is unused by the test.
static media::audio::ReadableStream::ReadLockContext rlctx;

constexpr uint32_t kDefaultFrameRate = 48000;
const Format kDefaultFormat =
    Format::Create(fuchsia::media::AudioStreamType{
                       .sample_format = fuchsia::media::AudioSampleFormat::FLOAT,
                       .channels = 2,
                       .frames_per_second = kDefaultFrameRate,
                   })
        .take_value();

const TimelineFunction kDefaultTransform = TimelineFunction(
    TimelineRate(Fixed(kDefaultFormat.frames_per_second()).raw_value(), zx::sec(1).to_nsecs()));

enum ClockMode { SAME, WITH_OFFSET, RATE_ADJUST };

class OutputPipelineTest : public testing::ThreadingModelFixture {
 protected:
  std::shared_ptr<OutputPipeline> CreateOutputPipeline(
      VolumeCurve volume_curve =
          VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume)) {
    ProcessConfig::Builder builder;
    PipelineConfig::MixGroup root{
        .name = "linearize",
        .input_streams =
            {
                RenderUsage::BACKGROUND,
            },
        .effects_v1 = {},
        .inputs = {{
            .name = "mix",
            .input_streams =
                {
                    RenderUsage::INTERRUPTION,
                },
            .effects_v1 = {},
            .inputs =
                {
                    {
                        .name = "default",
                        .input_streams =
                            {
                                RenderUsage::MEDIA,
                                RenderUsage::SYSTEM_AGENT,
                            },
                        .effects_v1 = {},
                        .loopback = false,
                        .output_rate = 48000,
                        .output_channels = 2,
                    },
                    {
                        .name = "communications",
                        .input_streams =
                            {
                                RenderUsage::COMMUNICATION,
                            },
                        .effects_v1 = {},
                        .loopback = false,
                        .output_rate = 48000,
                        .output_channels = 2,
                    },
                },
            .loopback = false,
            .output_rate = 48000,
            .output_channels = 2,

        }},
        .loopback = false,
        .output_rate = 48000,
        .output_channels = 2,
    };

    auto pipeline_config = PipelineConfig(root);
    return std::make_shared<OutputPipelineImpl>(pipeline_config, volume_curve,
                                                nullptr /* EffectsLoaderV2 */, 128,
                                                kDefaultTransform, device_clock_);
  }

  int64_t duration_to_frames(zx::duration delta) {
    return kDefaultFormat.frames_per_ns().Scale(delta.to_nsecs());
  }
  std::shared_ptr<Clock> SetPacketFactoryWithOffsetAudioClock(zx::duration clock_offset,
                                                              testing::PacketFactory& factory);
  std::shared_ptr<Clock> CreateClientClock() {
    return context().clock_factory()->CreateClientFixed(clock::AdjustableCloneOfMonotonic());
  }

  std::shared_ptr<testing::FakeStream> CreateFakeStream(StreamUsage stream_usage) {
    auto stream = std::make_shared<testing::FakeStream>(kDefaultFormat, context().clock_factory());
    stream->set_usage_mask({stream_usage});
    stream->set_gain_db(0.0);
    stream->timeline_function()->Update(kDefaultTransform);
    return stream;
  }

  // If tolerance is not supplied, we FLOAT_EQ compare.
  void CheckBuffer(void* buffer, float expected_sample, size_t num_samples,
                   float tolerance = 0.0f) {
    float* floats = reinterpret_cast<float*>(buffer);
    for (size_t i = 0; i < num_samples; ++i) {
      if (tolerance) {
        EXPECT_NEAR(expected_sample, floats[i], std::abs(tolerance))
            << "failed at sample " << i << " of " << num_samples << ": {"
            << ([floats, num_samples]() {
                 std::ostringstream out;
                 for (size_t i = 0; i < num_samples; ++i) {
                   out << floats[i] << ", ";
                 }
                 return out.str();
               })()
            << "}";
      } else {
        EXPECT_FLOAT_EQ(expected_sample, floats[i])
            << "failed at sample " << i << " of " << num_samples << ": {"
            << ([floats, num_samples]() {
                 std::ostringstream out;
                 for (size_t i = 0; i < num_samples; ++i) {
                   out << floats[i] << ", ";
                 }
                 return out.str();
               })()
            << "}";
      }
    }
  }

  void TestOutputPipelineTrim(ClockMode clock_mode);
  void TestDifferentMixRates(ClockMode clock_mode);

  std::shared_ptr<Clock> device_clock_ = context().clock_factory()->CreateDeviceFixed(
      clock::CloneOfMonotonic(), Clock::kMonotonicDomain);
};

std::shared_ptr<Clock> OutputPipelineTest::SetPacketFactoryWithOffsetAudioClock(
    zx::duration clock_offset, testing::PacketFactory& factory) {
  auto custom_clock_result =
      clock::testing::CreateCustomClock({.start_val = zx::clock::get_monotonic() + clock_offset});
  EXPECT_TRUE(custom_clock_result.is_ok());

  zx::clock custom_clock = custom_clock_result.take_value();

  auto actual_offset = clock::testing::GetOffsetFromMonotonic(custom_clock).take_value();

  int64_t seek_frame = round(
      static_cast<double>(kDefaultFormat.frames_per_second() * actual_offset.get()) / ZX_SEC(1));
  factory.SeekToFrame(Fixed(seek_frame));

  return context().clock_factory()->CreateClientFixed(std::move(custom_clock));
}

void OutputPipelineTest::TestOutputPipelineTrim(ClockMode clock_mode) {
  auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(kDefaultTransform);

  // We set up four different streams (PacketQueues), each with its own PacketFactory.
  // The last one might have a custom clock; the rest share a common "client_clock_".
  testing::PacketFactory packet_factory1(dispatcher(), kDefaultFormat, zx_system_get_page_size());
  testing::PacketFactory packet_factory2(dispatcher(), kDefaultFormat, zx_system_get_page_size());
  testing::PacketFactory packet_factory3(dispatcher(), kDefaultFormat, zx_system_get_page_size());
  testing::PacketFactory packet_factory4(dispatcher(), kDefaultFormat, zx_system_get_page_size());

  auto stream1 =
      std::make_shared<PacketQueue>(kDefaultFormat, timeline_function, CreateClientClock());
  auto stream2 =
      std::make_shared<PacketQueue>(kDefaultFormat, timeline_function, CreateClientClock());
  auto stream3 =
      std::make_shared<PacketQueue>(kDefaultFormat, timeline_function, CreateClientClock());
  std::shared_ptr<PacketQueue> stream4;

  if (clock_mode == ClockMode::SAME) {
    stream4 = std::make_shared<PacketQueue>(kDefaultFormat, timeline_function, CreateClientClock());
  } else if (clock_mode == ClockMode::WITH_OFFSET) {
    auto custom_audio_clock = SetPacketFactoryWithOffsetAudioClock(zx::sec(-3), packet_factory4);

    stream4 = std::make_shared<PacketQueue>(kDefaultFormat, timeline_function,
                                            std::move(custom_audio_clock));
  } else {
    ASSERT_TRUE(clock_mode == ClockMode::RATE_ADJUST) << "Unknown clock mode";
    ASSERT_TRUE(false) << "Multi-rate testing not yet implemented";
  }

  // Add some streams so that one is routed to each mix stage in our pipeline.
  auto pipeline = CreateOutputPipeline();
  pipeline->AddInput(stream1, StreamUsage::WithRenderUsage(RenderUsage::BACKGROUND));
  pipeline->AddInput(stream2, StreamUsage::WithRenderUsage(RenderUsage::INTERRUPTION));
  pipeline->AddInput(stream3, StreamUsage::WithRenderUsage(RenderUsage::MEDIA));
  pipeline->AddInput(stream4, StreamUsage::WithRenderUsage(RenderUsage::COMMUNICATION));

  bool packet_released[8] = {};
  {
    stream1->PushPacket(packet_factory1.CreatePacket(
        1.0, zx::msec(5), [&packet_released] { packet_released[0] = true; }));
    stream1->PushPacket(packet_factory1.CreatePacket(
        1.0, zx::msec(5), [&packet_released] { packet_released[1] = true; }));
  }
  {
    stream2->PushPacket(packet_factory2.CreatePacket(
        1.0, zx::msec(5), [&packet_released] { packet_released[2] = true; }));
    stream2->PushPacket(packet_factory2.CreatePacket(
        1.0, zx::msec(5), [&packet_released] { packet_released[3] = true; }));
  }
  {
    stream3->PushPacket(packet_factory3.CreatePacket(
        1.0, zx::msec(5), [&packet_released] { packet_released[4] = true; }));
    stream3->PushPacket(packet_factory3.CreatePacket(
        1.0, zx::msec(5), [&packet_released] { packet_released[5] = true; }));
  }
  {
    stream4->PushPacket(packet_factory4.CreatePacket(
        1.0, zx::msec(5), [&packet_released] { packet_released[6] = true; }));
    stream4->PushPacket(packet_factory4.CreatePacket(
        1.0, zx::msec(5), [&packet_released] { packet_released[7] = true; }));
  }

  // Because of how we set up custom clocks, we can't reliably Trim to a specific frame number (we
  // might be off by half a frame), so we allow ourselves one frame of tolerance either direction.
  constexpr int64_t kToleranceFrames = 1;

  // Before 5ms: no packet is entirely consumed; we should still retain all packets.
  pipeline->Trim(Fixed(duration_to_frames(zx::msec(5)) - kToleranceFrames));
  RunLoopUntilIdle();
  EXPECT_THAT(packet_released, Each(Eq(false)));

  // After 5ms: first packets are consumed and released. We should still retain the others.
  pipeline->Trim(Fixed(duration_to_frames(zx::msec(5)) + kToleranceFrames));
  RunLoopUntilIdle();
  EXPECT_THAT(packet_released,
              Pointwise(Eq(), {true, false, true, false, true, false, true, false}));

  // After 10ms we should have trimmed all the packets.
  pipeline->Trim(Fixed(duration_to_frames(zx::msec(10)) + kToleranceFrames));
  RunLoopUntilIdle();
  EXPECT_THAT(packet_released, Each(Eq(true)));

  // Upon any fail, slab_allocator asserts at exit. Clear all allocations, so testing can continue.
  pipeline->Trim(Fixed::Max());
}

TEST_F(OutputPipelineTest, Trim) { TestOutputPipelineTrim(ClockMode::SAME); }
TEST_F(OutputPipelineTest, Trim_ClockOffset) { TestOutputPipelineTrim(ClockMode::WITH_OFFSET); }

TEST_F(OutputPipelineTest, Loopback) {
  auto test_effects = testing::TestEffectsV1Module::Open();
  test_effects.AddEffect("add_1.0").WithAction(TEST_EFFECTS_ACTION_ADD, 1.0);
  PipelineConfig::MixGroup root{
      .name = "linearize",
      .input_streams =
          {
              RenderUsage::BACKGROUND,
          },
      .effects_v1 =
          {
              {
                  .lib_name = "test_effects_v1.so",
                  .effect_name = "add_1.0",
                  .instance_name = "",
                  .effect_config = "",
              },
          },
      .inputs = {{
          .name = "mix",
          .input_streams =
              {
                  RenderUsage::MEDIA,
                  RenderUsage::SYSTEM_AGENT,
                  RenderUsage::INTERRUPTION,
                  RenderUsage::COMMUNICATION,
              },
          .effects_v1 =
              {
                  {
                      .lib_name = "test_effects_v1.so",
                      .effect_name = "add_1.0",
                      .instance_name = "",
                      .effect_config = "",
                  },
              },
          .loopback = true,
          .output_rate = 48000,
          .output_channels = 2,
      }},
      .loopback = false,
      .output_rate = 48000,
      .output_channels = 2,
  };
  auto pipeline_config = PipelineConfig(root);
  auto volume_curve = VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume);
  auto pipeline = std::make_shared<OutputPipelineImpl>(pipeline_config, volume_curve,
                                                       nullptr /* EffectsLoaderV2 */, 128,
                                                       kDefaultTransform, device_clock_);

  // Add an input into our pipeline so that we have some frames to mix.
  const auto stream_usage = StreamUsage::WithRenderUsage(RenderUsage::MEDIA);
  pipeline->AddInput(CreateFakeStream(stream_usage), stream_usage);

  // Present frames ahead of now to stay ahead of the safe_write_frame.
  auto ref_start = device_clock_->now();
  auto loopback = pipeline->dup_loopback();
  auto transform = loopback->ref_time_to_frac_presentation_frame();
  auto loopback_frame = Fixed::FromRaw(transform.timeline_function.Apply(ref_start.get())).Floor();

  // Verify our stream from the pipeline has the effects applied (we have no input streams so we
  // should have silence with a two effects that adds 1.0 to each sample (one on the mix stage
  // and one on the linearize stage). Therefore we expect all samples to be 2.0.
  auto buf = pipeline->ReadLock(rlctx, Fixed(loopback_frame), 48);
  ASSERT_TRUE(buf);
  ASSERT_EQ(buf->start().Floor(), loopback_frame);
  ASSERT_EQ(buf->length(), 48);
  CheckBuffer(buf->payload(), 2.0, 96);

  // Advance time to our safe_read_frame past the above mix, which includes 1ms of output.
  context().clock_factory()->AdvanceMonoTimeBy(zx::msec(1));

  // We loopback after the mix stage and before the linearize stage. So we should observe only a
  // single effects pass. Therefore we expect all loopback samples to be 1.0.
  auto loopback_buf = loopback->ReadLock(rlctx, Fixed(loopback_frame), 48);
  ASSERT_TRUE(loopback_buf);
  ASSERT_EQ(loopback_buf->start().Floor(), loopback_frame);
  ASSERT_LE(loopback_buf->length(), 48);
  CheckBuffer(loopback_buf->payload(), 1.0, loopback_buf->length() * 2);

  if (loopback_buf->length() < 48u) {
    // The loopback read might need to wrap around the ring buffer. When this happens,
    // the first ReadLock returns fewer frames that we asked for. Verify we can read the
    // remaining frames instantly.
    loopback_frame += loopback_buf->length();
    auto frames_remaining = 48 - loopback_buf->length();
    loopback_buf = loopback->ReadLock(rlctx, Fixed(loopback_frame), frames_remaining);
    ASSERT_TRUE(loopback_buf);
    ASSERT_EQ(loopback_buf->start().Floor(), loopback_frame);
    ASSERT_EQ(loopback_buf->length(), frames_remaining);
    CheckBuffer(loopback_buf->payload(), 1.0, frames_remaining * 2);
  }
}

// Identical to |Loopback|, except we run mix and linearize stages at different rates.
TEST_F(OutputPipelineTest, LoopbackWithUpsample) {
  auto test_effects = testing::TestEffectsV1Module::Open();
  test_effects.AddEffect("add_1.0").WithAction(TEST_EFFECTS_ACTION_ADD, 1.0);
  PipelineConfig::MixGroup root{
      .name = "linearize",
      .input_streams =
          {
              RenderUsage::BACKGROUND,
          },
      .effects_v1 =
          {
              {
                  .lib_name = "test_effects_v1.so",
                  .effect_name = "add_1.0",
                  .instance_name = "",
                  .effect_config = "",
              },
          },
      .inputs = {{
          .name = "mix",
          .input_streams =
              {
                  RenderUsage::MEDIA,
                  RenderUsage::SYSTEM_AGENT,
                  RenderUsage::INTERRUPTION,
                  RenderUsage::COMMUNICATION,
              },
          .effects_v1 =
              {
                  {
                      .lib_name = "test_effects_v1.so",
                      .effect_name = "add_1.0",
                      .instance_name = "",
                      .effect_config = "",
                  },
              },
          .loopback = true,
          .output_rate = 48000,
          .output_channels = 2,
      }},
      .loopback = false,
      .output_rate = 96000,
      .output_channels = 2,
  };
  auto pipeline_config = PipelineConfig(root);
  auto volume_curve = VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume);
  auto pipeline = std::make_shared<OutputPipelineImpl>(pipeline_config, volume_curve,
                                                       nullptr /* EffectsLoaderV2 */, 128,
                                                       kDefaultTransform, device_clock_);

  // Add an input into our pipeline so that we have some frames to mix.
  const auto stream_usage = StreamUsage::WithRenderUsage(RenderUsage::MEDIA);
  pipeline->AddInput(CreateFakeStream(stream_usage), stream_usage);

  // Present frames ahead of now to stay ahead of the safe_write_frame.
  auto ref_start = device_clock_->now();
  auto loopback = pipeline->dup_loopback();
  auto transform = loopback->ref_time_to_frac_presentation_frame();
  auto loopback_frame = Fixed::FromRaw(transform.timeline_function.Apply(ref_start.get())).Floor();

  // Verify our stream from the pipeline has the effects applied (we have no input streams so we
  // should have silence with a two effects that adds 1.0 to each sample (one on the mix stage
  // and one on the linearize stage). Therefore we expect all samples to be 2.0.
  auto buf = pipeline->ReadLock(rlctx, Fixed(loopback_frame), 96);
  ASSERT_TRUE(buf);
  ASSERT_EQ(buf->start().Floor(), loopback_frame);
  ASSERT_EQ(buf->length(), 96);
  CheckBuffer(buf->payload(), 2.0, 192);

  // Advance time to our safe_read_frame past the above mix, which includes 1ms of output.
  context().clock_factory()->AdvanceMonoTimeBy(zx::msec(1));
  // We loopback after the mix stage and before the linearize stage. So we should observe only a
  // single effects pass. Therefore we expect all loopback samples to be 1.0.
  auto loopback_buf = loopback->ReadLock(rlctx, Fixed(loopback_frame), 48);
  ASSERT_TRUE(loopback_buf);
  ASSERT_EQ(loopback_buf->start().Floor(), loopback_frame);
  ASSERT_LE(loopback_buf->length(), 48);
  CheckBuffer(loopback_buf->payload(), 1.0, loopback_buf->length() * 2);

  if (loopback_buf->length() < 48u) {
    // The loopback read might need to wrap around the ring buffer. When this happens,
    // the first ReadLock returns fewer frames that we asked for. Verify we can read the
    // remaining frames instantly.
    loopback_frame += loopback_buf->length();
    auto frames_remaining = 48 - loopback_buf->length();
    loopback_buf = loopback->ReadLock(rlctx, Fixed(loopback_frame), frames_remaining);
    ASSERT_TRUE(loopback_buf);
    ASSERT_EQ(loopback_buf->start().Floor(), loopback_frame);
    ASSERT_EQ(loopback_buf->length(), frames_remaining);
    CheckBuffer(loopback_buf->payload(), 1.0, frames_remaining * 2);
  }
}

static const std::string kInstanceName = "instance name";
static const std::string kConfig = "config";

TEST_F(OutputPipelineTest, UpdateEffect) {
  auto test_effects = testing::TestEffectsV1Module::Open();
  test_effects.AddEffect("assign_config_size")
      .WithAction(TEST_EFFECTS_ACTION_ASSIGN_CONFIG_SIZE, 0.0);
  PipelineConfig::MixGroup root{
      .name = "linearize",
      .input_streams =
          {
              RenderUsage::BACKGROUND,
          },
      .effects_v1 =
          {
              {
                  .lib_name = "test_effects_v1.so",
                  .effect_name = "assign_config_size",
                  .instance_name = kInstanceName,
                  .effect_config = "",
              },
          },
      .inputs = {{
          .name = "mix",
          .input_streams =
              {
                  RenderUsage::MEDIA,
                  RenderUsage::SYSTEM_AGENT,
                  RenderUsage::INTERRUPTION,
                  RenderUsage::COMMUNICATION,
              },
          .effects_v1 = {},
          .output_rate = 48000,
          .output_channels = 2,
      }},
      .output_rate = 48000,
      .output_channels = 2,
  };
  auto pipeline_config = PipelineConfig(root);
  auto volume_curve = VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume);
  auto pipeline = std::make_shared<OutputPipelineImpl>(pipeline_config, volume_curve,
                                                       nullptr /* EffectsLoaderV2 */, 128,
                                                       kDefaultTransform, device_clock_);

  // Add an input into our pipeline so that we have some frames to mix.
  const auto stream_usage = StreamUsage::WithRenderUsage(RenderUsage::MEDIA);
  pipeline->AddInput(CreateFakeStream(stream_usage), stream_usage);

  pipeline->UpdateEffect(kInstanceName, kConfig);

  // Verify our stream from the pipeline has the effects applied (we have no input streams so we
  // should have silence with a single effect that sets all samples to the size of the new config).
  auto buf = pipeline->ReadLock(rlctx, Fixed(0), 48);
  ASSERT_TRUE(buf);
  ASSERT_EQ(buf->start().Floor(), 0u);
  ASSERT_EQ(buf->length(), 48);
  float expected_sample = static_cast<float>(kConfig.size());
  CheckBuffer(buf->payload(), expected_sample, 96);
}

// This test makes assumptions about the mixer's lead-time, so we explicitly specify the
// SampleAndHold resampler. Because we compare actual duration to expected duration down to the
// nanosec, the amount of delay in our test effects is carefully chosen and may be brittle.
TEST_F(OutputPipelineTest, ReportPresentationDelay) {
  constexpr int64_t kEffects1LeadTimeFrames = 300;
  constexpr int64_t kEffects2LeadTimeFrames = 900;

  auto test_effects = testing::TestEffectsV1Module::Open();
  test_effects.AddEffect("effect_with_delay_300").WithSignalLatencyFrames(kEffects1LeadTimeFrames);
  test_effects.AddEffect("effect_with_delay_900").WithSignalLatencyFrames(kEffects2LeadTimeFrames);
  PipelineConfig::MixGroup root{
      .name = "linearize",
      .input_streams =
          {
              RenderUsage::BACKGROUND,
          },
      .effects_v1 = {},
      .inputs = {{
                     .name = "default",
                     .input_streams =
                         {
                             RenderUsage::MEDIA,
                             RenderUsage::SYSTEM_AGENT,
                             RenderUsage::INTERRUPTION,
                         },
                     .effects_v1 =
                         {
                             {
                                 .lib_name = "test_effects_v1.so",
                                 .effect_name = "effect_with_delay_300",
                                 .effect_config = "",
                             },
                         },
                     .output_rate = kDefaultFormat.frames_per_second(),
                     .output_channels = 2,
                 },
                 {
                     .name = "communications",
                     .input_streams =
                         {
                             RenderUsage::COMMUNICATION,
                         },
                     .effects_v1 =
                         {
                             {
                                 .lib_name = "test_effects_v1.so",
                                 .effect_name = "effect_with_delay_900",
                                 .effect_config = "",
                             },
                         },
                     .output_rate = kDefaultFormat.frames_per_second(),
                     .output_channels = 2,
                 }},
      .output_rate = kDefaultFormat.frames_per_second(),
      .output_channels = 2,
  };
  auto pipeline_config = PipelineConfig(root);
  auto volume_curve = VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume);
  auto pipeline = std::make_shared<OutputPipelineImpl>(
      pipeline_config, volume_curve, nullptr /* EffectsLoaderV2 */, 128, kDefaultTransform,
      device_clock_, Mixer::Resampler::SampleAndHold);

  // Add 2 streams, one with a MEDIA usage and one with COMMUNICATION usage. These should receive
  // different lead times since they have different effects (with different latencies) applied.
  auto default_stream =
      std::make_shared<testing::FakeStream>(kDefaultFormat, context().clock_factory());
  auto default_mixer =
      pipeline->AddInput(default_stream, StreamUsage::WithRenderUsage(RenderUsage::MEDIA),
                         std::nullopt, Mixer::Resampler::SampleAndHold);
  const int64_t kMixLeadTimeFrames = default_mixer->pos_filter_width().Ceiling();

  auto communications_stream =
      std::make_shared<testing::FakeStream>(kDefaultFormat, context().clock_factory());
  pipeline->AddInput(communications_stream,
                     StreamUsage::WithRenderUsage(RenderUsage::COMMUNICATION), std::nullopt,
                     Mixer::Resampler::SampleAndHold);

  // The pipeline itself (the root, after any MixStages or EffectsStages) requires no lead time.
  EXPECT_EQ(zx::duration(0), pipeline->GetPresentationDelay());

  // MEDIA streams require 302 frames of lead time. They run through an effect that introduces 300
  // frames of delay; SampleAndHold resamplers in the 'default' and 'linearize' MixStages each
  // add 1 frame of lead time.
  const auto default_delay = zx::duration(kDefaultFormat.frames_per_ns().Inverse().Scale(
      kMixLeadTimeFrames + kEffects1LeadTimeFrames + kMixLeadTimeFrames));
  EXPECT_EQ(default_delay, default_stream->GetPresentationDelay())
      << default_delay.get() << ", " << default_stream->GetPresentationDelay().get() << ", off by "
      << (default_delay - default_stream->GetPresentationDelay()).get() << " nsec";

  // COMMUNICATION streams require 902 frames of lead time. They run through an effect that
  // introduces 900 frames of delay; SampleAndHold resamplers in the 'default' and 'linearize'
  // MixStages each add 1 frame of lead time.
  const auto communications_delay = zx::duration(
      zx::sec(kMixLeadTimeFrames + kEffects2LeadTimeFrames + kMixLeadTimeFrames).to_nsecs() /
      kDefaultFormat.frames_per_second());
  EXPECT_EQ(communications_delay, communications_stream->GetPresentationDelay())
      << communications_delay.get() << ", " << communications_stream->GetPresentationDelay().get()
      << ", off by " << (communications_delay - communications_stream->GetPresentationDelay()).get()
      << " nsec";
}

void* SampleOffset(void* ptr, int64_t offset) {
  return reinterpret_cast<void*>(reinterpret_cast<float*>(ptr) + offset);
}

void OutputPipelineTest::TestDifferentMixRates(ClockMode clock_mode) {
  constexpr auto kChannelCount = 2;
  static const PipelineConfig::MixGroup root{
      .name = "linearize",
      .input_streams =
          {
              RenderUsage::BACKGROUND,
          },
      .inputs = {{
          .name = "mix",
          .input_streams =
              {
                  RenderUsage::MEDIA,
                  RenderUsage::SYSTEM_AGENT,
                  RenderUsage::INTERRUPTION,
                  RenderUsage::COMMUNICATION,
              },
          .effects_v1 = {},
          .loopback = true,
          .output_rate = 24000,
          .output_channels = kChannelCount,
      }},
      .loopback = false,
      .output_rate = 48000,
      .output_channels = kChannelCount,
  };

  // Add the stream with a usage that routes to the mix stage. Our stream will be rate-converted, so
  // we cannot use SampleAndHold -- we must use WindowedSinc.
  const Mixer::Resampler resampler = Mixer::Resampler::WindowedSinc;
  auto timeline_function = fbl::MakeRefCounted<VersionedTimelineFunction>(kDefaultTransform);

  testing::PacketFactory packet_factory(dispatcher(), kDefaultFormat,
                                        2 * zx_system_get_page_size());
  std::shared_ptr<PacketQueue> stream;

  if (clock_mode == ClockMode::SAME) {
    stream = std::make_shared<PacketQueue>(kDefaultFormat, timeline_function, CreateClientClock());
  } else if (clock_mode == ClockMode::WITH_OFFSET) {
    auto custom_audio_clock = SetPacketFactoryWithOffsetAudioClock(zx::sec(7), packet_factory);

    stream = std::make_shared<PacketQueue>(kDefaultFormat, timeline_function,
                                           std::move(custom_audio_clock));
  } else {
    ASSERT_TRUE(clock_mode == ClockMode::RATE_ADJUST) << "Unknown clock mode";
    ASSERT_TRUE(false) << "Multi-rate testing not yet implemented";
  }

  auto pipeline_config = PipelineConfig(root);
  auto volume_curve = VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume);
  auto pipeline = std::make_shared<OutputPipelineImpl>(pipeline_config, volume_curve,
                                                       nullptr /* EffectsLoaderV2 */, 480,
                                                       kDefaultTransform, device_clock_, resampler);

  pipeline->AddInput(stream, StreamUsage::WithRenderUsage(RenderUsage::MEDIA), std::nullopt,
                     resampler);

  bool packet_released[3] = {};
  constexpr auto kFramesPerRead = 240;

  constexpr auto kVal1 = 1.0f;
  constexpr auto kVal2 = -1.0f;

  // Count of transition frames between kVal1 and kVal2 in the output. Sinc samplers will converge
  // within a filter width; here we expect a settling to +/-7.5% in about 5 frames.
  constexpr auto kNumTransitionFrames = 5;
  constexpr auto kValTolerance = 0.075f;

  constexpr auto sample_start = kNumTransitionFrames * kChannelCount;
  constexpr auto sample_end = (kFramesPerRead - kNumTransitionFrames) * kChannelCount;
  constexpr auto sample_length = sample_end - sample_start;
  {
    stream->PushPacket(packet_factory.CreatePacket(
        kVal1, zx::msec(5), [&packet_released] { packet_released[0] = true; }));
    stream->PushPacket(packet_factory.CreatePacket(
        kVal2, zx::msec(5), [&packet_released] { packet_released[1] = true; }));

    // We push an extra packet so this test won't need to worry about ring-out values.
    stream->PushPacket(packet_factory.CreatePacket(
        kVal2, zx::msec(5), [&packet_released] { packet_released[2] = true; }));
  }

  {
    SCOPED_TRACE("Read1");
    auto buf = pipeline->ReadLock(rlctx, Fixed(0), kFramesPerRead);
    RunLoopUntilIdle();

    EXPECT_TRUE(buf);
    EXPECT_TRUE(packet_released[0]);
    EXPECT_FALSE(packet_released[1]);

    EXPECT_EQ(buf->start().Floor(), 0);
    EXPECT_EQ(buf->length(), kFramesPerRead);
    auto arr = SampleOffset(buf->payload(), sample_start);
    CheckBuffer(arr, kVal1, sample_length, kValTolerance * kVal1);
  }

  {
    SCOPED_TRACE("Read");
    auto buf = pipeline->ReadLock(rlctx, Fixed(kFramesPerRead), kFramesPerRead);
    RunLoopUntilIdle();

    EXPECT_TRUE(buf);
    EXPECT_TRUE(packet_released[1]);
    EXPECT_FALSE(packet_released[2]);

    EXPECT_EQ(buf->start().Floor(), kFramesPerRead);
    EXPECT_EQ(buf->length(), kFramesPerRead);

    auto arr = SampleOffset(buf->payload(), sample_start);
    CheckBuffer(arr, kVal2, sample_length, kValTolerance * kVal2);
  }

  {
    SCOPED_TRACE("Read3");
    auto buf = pipeline->ReadLock(rlctx, Fixed(kFramesPerRead * 2), kFramesPerRead);
    RunLoopUntilIdle();

    EXPECT_TRUE(buf);
    EXPECT_TRUE(packet_released[2]);
  }
}

TEST_F(OutputPipelineTest, DifferentMixRates) { TestDifferentMixRates(ClockMode::SAME); }
TEST_F(OutputPipelineTest, DifferentMixRates_ClockOffset) {
  TestDifferentMixRates(ClockMode::WITH_OFFSET);
}

TEST_F(OutputPipelineTest, PipelineWithRechannelEffects) {
  auto test_effects = testing::TestEffectsV1Module::Open();
  test_effects.AddEffect("add_1.0").WithAction(TEST_EFFECTS_ACTION_ADD, 1.0);
  PipelineConfig::MixGroup root{
      .name = "linearize",
      .input_streams =
          {
              RenderUsage::BACKGROUND,
          },
      .effects_v1 =
          {
              {
                  .lib_name = "test_effects_v1.so",
                  .effect_name = "add_1.0",
                  .instance_name = "",
                  .effect_config = "",
                  .output_channels = 4,
              },
          },
      .inputs = {{
          .name = "mix",
          .input_streams =
              {
                  RenderUsage::MEDIA,
                  RenderUsage::SYSTEM_AGENT,
                  RenderUsage::INTERRUPTION,
                  RenderUsage::COMMUNICATION,
              },
          .effects_v1 =
              {
                  {
                      .lib_name = "test_effects_v1.so",
                      .effect_name = "add_1.0",
                      .instance_name = "",
                      .effect_config = "",
                  },
              },
          .loopback = true,
          .output_rate = 48000,
          .output_channels = 2,
      }},
      .loopback = false,
      .output_rate = 48000,
      .output_channels = 2,
  };
  auto pipeline_config = PipelineConfig(root);
  auto volume_curve = VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume);
  auto pipeline = std::make_shared<OutputPipelineImpl>(pipeline_config, volume_curve,
                                                       nullptr /* EffectsLoaderV2 */, 128,
                                                       kDefaultTransform, device_clock_);

  // Verify the pipeline format includes the rechannel effect.
  EXPECT_EQ(4, pipeline->format().channels());
  EXPECT_EQ(48000, pipeline->format().frames_per_second());
  EXPECT_EQ(fuchsia::media::AudioSampleFormat::FLOAT, pipeline->format().sample_format());
}

TEST_F(OutputPipelineTest, LoopbackClock) {
  auto test_effects = testing::TestEffectsV1Module::Open();
  test_effects.AddEffect("add_1.0").WithAction(TEST_EFFECTS_ACTION_ADD, 1.0);
  PipelineConfig::MixGroup root{
      .name = "linearize",
      .input_streams =
          {
              RenderUsage::BACKGROUND,
          },
      .effects_v1 =
          {
              {
                  .lib_name = "test_effects_v1.so",
                  .effect_name = "add_1.0",
                  .instance_name = "",
                  .effect_config = "",
              },
          },
      .inputs = {{
          .name = "mix",
          .input_streams =
              {
                  RenderUsage::MEDIA,
                  RenderUsage::SYSTEM_AGENT,
                  RenderUsage::INTERRUPTION,
                  RenderUsage::COMMUNICATION,
              },
          .effects_v1 =
              {
                  {
                      .lib_name = "test_effects_v1.so",
                      .effect_name = "add_1.0",
                      .instance_name = "",
                      .effect_config = "",
                  },
              },
          .loopback = true,
          .output_rate = 48000,
          .output_channels = 2,
      }},
      .loopback = false,
      .output_rate = 48000,
      .output_channels = 2,
  };
  auto pipeline_config = PipelineConfig(root);
  auto volume_curve = VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume);
  auto pipeline = std::make_shared<OutputPipelineImpl>(pipeline_config, volume_curve,
                                                       nullptr /* EffectsLoaderV2 */, 128,
                                                       kDefaultTransform, device_clock_);

  clock::testing::VerifyReadOnlyRights(*pipeline->reference_clock());
  clock::testing::VerifyAdvances(*pipeline->reference_clock(),
                                 context().clock_factory()->synthetic());
  clock::testing::VerifyCannotBeRateAdjusted(*pipeline->reference_clock());

  auto& loopback_clock = *pipeline->dup_loopback()->reference_clock();
  clock::testing::VerifyReadOnlyRights(loopback_clock);
  clock::testing::VerifyAdvances(loopback_clock, context().clock_factory()->synthetic());
  clock::testing::VerifyCannotBeRateAdjusted(loopback_clock);
  ASSERT_TRUE(pipeline->reference_clock()->koid() == loopback_clock.koid());
}

TEST_F(OutputPipelineTest, PipelineWithEffectsV2) {
  fidl::Arena arena;
  TestEffectsV2 test_effects;
  test_effects.AddEffect({
      .name = "AddOne",
      .process =
          [&arena](
              uint64_t num_frames, float* input, float* output, float total_applied_gain_for_input,
              std::vector<fuchsia_audio_effects::wire::ProcessMetrics>& metrics_vector) mutable {
            for (uint64_t k = 0; k < num_frames; k++) {
              for (int c = 0; c < 2; c++) {
                output[k * 2 + c] = input[k * 2 + c] + 1;
              }
            }
            fuchsia_audio_effects::wire::ProcessMetrics metrics(arena);
            metrics.set_name(arena, "stage");
            metrics.set_wall_time(arena, 10);
            metrics.set_cpu_time(arena, 100);
            metrics_vector.push_back(std::move(metrics));
            return ZX_OK;
          },
      .process_in_place = false,
      .max_frames_per_call = 128,
      .frames_per_second = 48000,
      .input_channels = 2,
      .output_channels = 2,
  });

  auto effects_loader_v2 = EffectsLoaderV2::CreateFromChannel(test_effects.NewClient());
  ASSERT_TRUE(effects_loader_v2.is_ok());

  PipelineConfig::MixGroup root{
      .name = "linearize",
      .input_streams =
          {
              RenderUsage::BACKGROUND,
          },
      .effects_v2 = PipelineConfig::EffectV2{.instance_name = "AddOne"},
      .inputs = {{
          .name = "mix",
          .input_streams =
              {
                  RenderUsage::MEDIA,
                  RenderUsage::SYSTEM_AGENT,
                  RenderUsage::INTERRUPTION,
                  RenderUsage::COMMUNICATION,
              },
          .effects_v2 = PipelineConfig::EffectV2{.instance_name = "AddOne"},
          .loopback = true,
          .output_rate = 48000,
          .output_channels = 2,
      }},
      .loopback = false,
      .output_rate = 48000,
      .output_channels = 2,
  };
  auto pipeline_config = PipelineConfig(root);
  auto volume_curve = VolumeCurve::DefaultForMinGain(VolumeCurve::kDefaultGainForMinVolume);
  auto pipeline = std::make_shared<OutputPipelineImpl>(pipeline_config, volume_curve,
                                                       effects_loader_v2.value().get(), 128,
                                                       kDefaultTransform, device_clock_);

  // Add an input into our pipeline so that we have some frames to mix.
  const auto stream_usage = StreamUsage::WithRenderUsage(RenderUsage::MEDIA);
  pipeline->AddInput(CreateFakeStream(stream_usage), stream_usage);

  // Present frames ahead of now to stay ahead of the safe_write_frame.
  auto ref_start = device_clock_->now();
  auto loopback = pipeline->dup_loopback();
  auto transform = loopback->ref_time_to_frac_presentation_frame();
  auto loopback_frame = Fixed::FromRaw(transform.timeline_function.Apply(ref_start.get())).Floor();

  // Read 1ms worth of frames and verify the effects have been applied. The fake input
  // stream produces silent audio, so after two +1 effects, all samples should be 2.0.
  {
    SCOPED_TRACE("pipeline->ReadLock");
    ReadableStream::ReadLockContext rlctx;
    auto buf = pipeline->ReadLock(rlctx, Fixed(loopback_frame), 48);
    ASSERT_TRUE(buf);
    ASSERT_EQ(buf->start().Floor(), loopback_frame);
    ASSERT_EQ(buf->length(), 48);
    CheckBuffer(buf->payload(), 2.0, 96);

    // Check metrics: must have called the effect twice.
    ASSERT_EQ(rlctx.per_stage_metrics().size(), 3u);
    EXPECT_EQ(std::string_view(rlctx.per_stage_metrics()[0].name), "Mixer::Mix");
    EXPECT_EQ(std::string_view(rlctx.per_stage_metrics()[1].name), "EffectsStageV2::Process");
    EXPECT_EQ(std::string_view(rlctx.per_stage_metrics()[2].name), "stage");
    EXPECT_EQ(rlctx.per_stage_metrics()[2].wall_time.get(), 20);
    EXPECT_EQ(rlctx.per_stage_metrics()[2].cpu_time.get(), 200);
  }

  // Advance time to our safe_read_frame past the above ReadLock.
  context().clock_factory()->AdvanceMonoTimeBy(zx::msec(1));

  // We loopback after the mix stage and before the linearize stage. So we should observe only a
  // single effects pass. Therefore we expect all loopback samples to be 1.0.
  {
    SCOPED_TRACE("loopback->ReadLock");
    auto loopback_buf = loopback->ReadLock(rlctx, Fixed(loopback_frame), 48);
    ASSERT_TRUE(loopback_buf);
    ASSERT_EQ(loopback_buf->start().Floor(), loopback_frame);
    ASSERT_LE(loopback_buf->length(), 48);
    CheckBuffer(loopback_buf->payload(), 1.0, loopback_buf->length() * 2);

    if (loopback_buf->length() < 48u) {
      SCOPED_TRACE("loopback->ReadLock second call");
      // The loopback read might need to wrap around the ring buffer. When this happens,
      // the first ReadLock returns fewer frames that we asked for. Verify we can read the
      // remaining frames instantly.
      loopback_frame += loopback_buf->length();
      auto frames_remaining = 48 - loopback_buf->length();
      loopback_buf = loopback->ReadLock(rlctx, Fixed(loopback_frame), frames_remaining);
      ASSERT_TRUE(loopback_buf);
      ASSERT_EQ(loopback_buf->start().Floor(), loopback_frame);
      ASSERT_EQ(loopback_buf->length(), frames_remaining);
      CheckBuffer(loopback_buf->payload(), 1.0, frames_remaining * 2);
    }
  }
}

}  // namespace
}  // namespace media::audio

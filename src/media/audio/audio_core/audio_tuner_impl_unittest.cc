// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/audio_tuner_impl.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/audio_core/testing/threading_model_fixture.h"
#include "src/media/audio/lib/effects_loader/testing/test_effects.h"

using ::testing::AllOf;
using ::testing::Field;

namespace media::audio {
namespace {

const auto kDeviceIdString = "eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee";
const auto kDeviceIdUnique = AudioDevice::UniqueIdFromString(kDeviceIdString).take_value();

const auto kVolumeCurve = VolumeCurve::DefaultForMinGain(-160.0f);
const auto kDefaultConfig = ProcessConfig::Builder().SetDefaultVolumeCurve(kVolumeCurve).Build();

std::optional<fuchsia::media::tuning::StreamType> StreamTypeFromRenderUsage(RenderUsage usage) {
  switch (usage) {
    case RenderUsage::BACKGROUND:
      return fuchsia::media::tuning::StreamType::RENDER_BACKGROUND;
    case RenderUsage::MEDIA:
      return fuchsia::media::tuning::StreamType::RENDER_MEDIA;
    case RenderUsage::INTERRUPTION:
      return fuchsia::media::tuning::StreamType::RENDER_INTERRUPTION;
    case RenderUsage::SYSTEM_AGENT:
      return fuchsia::media::tuning::StreamType::RENDER_SYSTEM_AGENT;
    case RenderUsage::COMMUNICATION:
      return fuchsia::media::tuning::StreamType::RENDER_COMMUNICATION;
    case RenderUsage::ULTRASOUND:
      return fuchsia::media::tuning::StreamType::RENDER_ULTRASOUND;
    default:
      return std::nullopt;
  }
}

void ExpectEq(const VolumeCurve& expected,
              const std::vector<fuchsia::media::tuning::Volume> result) {
  std::vector<VolumeCurve::VolumeMapping> expected_mappings = expected.mappings();
  EXPECT_EQ(expected_mappings.size(), result.size());
  for (size_t i = 0; i < expected_mappings.size(); ++i) {
    EXPECT_EQ(expected_mappings[i].volume, result[i].level);
    EXPECT_EQ(expected_mappings[i].gain_dbfs, result[i].decibel);
  }
}

void ExpectEq(const PipelineConfig::Effect& expected,
              const fuchsia::media::tuning::AudioEffectConfig& result) {
  EXPECT_EQ(expected.lib_name, result.type().module_name());
  EXPECT_EQ(expected.effect_name, result.type().effect_name());
  EXPECT_EQ(expected.instance_name, result.instance_name());
  EXPECT_EQ(expected.effect_config, result.configuration());
  EXPECT_EQ(expected.output_channels, result.output_channels());
}

void ExpectEq(const PipelineConfig::MixGroup& expected,
              const fuchsia::media::tuning::AudioMixGroup& result) {
  EXPECT_EQ(expected.name, result.name);
  EXPECT_EQ(expected.loopback, result.loopback);
  EXPECT_EQ(expected.input_streams.size(), result.streams.size());
  for (size_t i = 0; i < expected.input_streams.size(); ++i) {
    auto expected_usage = expected.input_streams[i];
    auto result_usage = result.streams[i];
    EXPECT_EQ(StreamTypeFromRenderUsage(expected_usage), result_usage);
  }
  EXPECT_EQ(expected.effects.size(), result.effects.size());
  for (size_t i = 0; i < expected.effects.size(); ++i) {
    ExpectEq(expected.effects[i], result.effects[i]);
  }
  EXPECT_EQ(expected.inputs.size(), result.inputs.size());
  for (size_t i = 0; i < expected.inputs.size(); ++i) {
    ExpectEq(expected.inputs[i], std::move(*result.inputs[i]));
  }
  EXPECT_EQ(expected.output_rate, result.output_rate);
  EXPECT_EQ(expected.output_channels, result.output_channels);
}

class AudioTunerTest : public gtest::TestLoopFixture {
 protected:
  std::unique_ptr<Context> CreateContext(ProcessConfig process_config) {
    auto handle = ProcessConfig::set_instance(process_config);
    auto threading_model = std::make_unique<testing::TestThreadingModel>(&test_loop());
    sys::testing::ComponentContextProvider component_context_provider_;
    auto plug_detector = std::make_unique<testing::FakePlugDetector>();
    return Context::Create(std::move(threading_model), component_context_provider_.TakeContext(),
                           std::move(plug_detector), process_config);
  }

  testing::TestEffectsModule test_effects_ = testing::TestEffectsModule::Open();
};

TEST_F(AudioTunerTest, GetAvailableAudioEffects) {
  auto context = CreateContext(kDefaultConfig);
  AudioTunerImpl under_test(*context);

  // Create an effect we can load.
  test_effects_.AddEffect("test_effect");

  bool received_test_effect = false;
  under_test.GetAvailableAudioEffects(
      [&received_test_effect](std::vector<fuchsia::media::tuning::AudioEffectType> effects) {
        for (size_t i = 0; i < effects.size(); ++i) {
          if (effects[i].module_name() == testing::kTestEffectsModuleName &&
              effects[i].effect_name() == "test_effect") {
            received_test_effect = true;
          }
        }
      });
  EXPECT_TRUE(received_test_effect);
}

TEST_F(AudioTunerTest, GetAudioDeviceTuningProfile) {
  auto expected_process_config =
      ProcessConfigBuilder()
          .SetDefaultVolumeCurve(VolumeCurve::DefaultForMinGain(-60.0))
          .AddDeviceProfile(
              {std::vector<audio_stream_unique_id_t>{kDeviceIdUnique},
               DeviceConfig::OutputDeviceProfile(
                   /*eligible_for_loopback=*/true, /*supported_usages=*/{},
                   /*independent_volume_control=*/false,
                   PipelineConfig(PipelineConfig::MixGroup{
                       .name = "linearize",
                       .input_streams = {RenderUsage::BACKGROUND, RenderUsage::MEDIA},
                       .effects = {PipelineConfig::Effect{.lib_name = "my_effects.so",
                                                          .effect_name = "equalizer",
                                                          .instance_name = "eq1",
                                                          .effect_config = "",
                                                          .output_channels = 2}},
                       .inputs = {PipelineConfig::MixGroup{
                           .name = "mix",
                           .input_streams = {},
                           .effects = {},
                           .inputs = {PipelineConfig::MixGroup{.name = "output_streams",
                                                               .input_streams = {},
                                                               .effects = {},
                                                               .inputs = {},
                                                               .loopback = false,
                                                               .output_rate = 48000,
                                                               .output_channels = 2}},
                           .loopback = false,
                           .output_rate = 48000,
                           .output_channels = 2}},
                       .loopback = true,
                       .output_rate = 48000,
                       .output_channels = 2}))})
          .Build();

  auto context = CreateContext(expected_process_config);
  AudioTunerImpl under_test(*context);

  fuchsia::media::tuning::AudioDeviceTuningProfile tuning_profile;
  under_test.GetAudioDeviceProfile(
      kDeviceIdString, [&tuning_profile](fuchsia::media::tuning::AudioDeviceTuningProfile profile) {
        tuning_profile = std::move(profile);
      });

  VolumeCurve expected_curve = expected_process_config.default_volume_curve();
  std::vector<fuchsia::media::tuning::Volume> result_curve = tuning_profile.volume_curve();
  ExpectEq(expected_curve, result_curve);

  PipelineConfig::MixGroup expected_pipeline = expected_process_config.device_config()
                                                   .output_device_profile(kDeviceIdUnique)
                                                   .pipeline_config()
                                                   .root();
  ExpectEq(expected_pipeline, std::move(tuning_profile.pipeline()));
}

TEST_F(AudioTunerTest, GetDefaultAudioDeviceProfile) {
  auto expected_process_config = kDefaultConfig;
  auto context = CreateContext(expected_process_config);
  AudioTunerImpl under_test(*context);
  fuchsia::media::tuning::AudioDeviceTuningProfile tuning_profile;
  under_test.GetDefaultAudioDeviceProfile(
      kDeviceIdString, [&tuning_profile](fuchsia::media::tuning::AudioDeviceTuningProfile profile) {
        tuning_profile = std::move(profile);
      });

  VolumeCurve expected_curve = expected_process_config.default_volume_curve();
  std::vector<fuchsia::media::tuning::Volume> result_curve = tuning_profile.volume_curve();
  ExpectEq(expected_curve, result_curve);

  PipelineConfig::MixGroup expected_pipeline = expected_process_config.device_config()
                                                   .output_device_profile(kDeviceIdUnique)
                                                   .pipeline_config()
                                                   .root();
  ExpectEq(expected_pipeline, std::move(tuning_profile.pipeline()));
}

}  // namespace
}  // namespace media::audio

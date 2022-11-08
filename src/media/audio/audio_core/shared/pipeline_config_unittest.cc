// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/shared/pipeline_config.h"

#include <gtest/gtest.h>

#include "src/media/audio/effects/test_effects/test_effects_v2.h"
#include "src/media/audio/lib/effects_loader/effects_loader_v2.h"

namespace media::audio {
namespace {

zx_status_t NopEffect(uint64_t num_frames, float* input, float* output,
                      float total_applied_gain_for_input,
                      std::vector<fuchsia_audio_effects::wire::ProcessMetrics>& metrics) {
  return ZX_OK;
}

}  // namespace

TEST(PipelineConfigTest, CalculateChannelsDefaultNoEffects) {
  auto config = PipelineConfig::Default();

  // No effects, the pipeline channelization is the same as the output of the root mix stage.
  auto format = config.OutputFormat(nullptr);
  EXPECT_EQ(format.sample_format(), fuchsia::media::AudioSampleFormat::FLOAT);
  EXPECT_EQ(format.channels(), PipelineConfig::kDefaultMixGroupChannels);
  EXPECT_EQ(format.frames_per_second(), PipelineConfig::kDefaultMixGroupRate);
}

TEST(PipelineConfigTest, CalculateChannelsV1) {
  auto config = PipelineConfig::Default();

  // With rechannelization effects, the last effect defines the channelization.
  config.mutable_root().effects_v1.push_back(
      {"lib.so", "effect", "e1", "", {PipelineConfig::kDefaultMixGroupChannels + 1}});
  config.mutable_root().effects_v1.push_back(
      {"lib.so", "effect", "e2", "", {PipelineConfig::kDefaultMixGroupChannels + 2}});

  auto format = config.OutputFormat(nullptr);
  EXPECT_EQ(format.sample_format(), fuchsia::media::AudioSampleFormat::FLOAT);
  EXPECT_EQ(format.channels(), PipelineConfig::kDefaultMixGroupChannels + 2);
  EXPECT_EQ(format.frames_per_second(), PipelineConfig::kDefaultMixGroupRate);
}

TEST(PipelineConfigTest, CalculateChannelsV2) {
  constexpr int16_t kOutputChannelsForEffect = PipelineConfig::kDefaultMixGroupChannels + 1;

  // Add a simple effect to test_effects.so.
  TestEffectsV2 test_effects;
  test_effects.AddEffect({
      .name = "Nop",
      .process = &NopEffect,
      .process_in_place = false,
      .max_frames_per_call = 10,
      .frames_per_second = 48000,
      .input_channels = 1,
      .output_channels = kOutputChannelsForEffect,
  });

  auto loader_result = EffectsLoaderV2::CreateFromChannel(test_effects.NewClient());
  ASSERT_TRUE(loader_result.is_ok());

  auto config = PipelineConfig::Default();
  config.mutable_root().effects_v2 = PipelineConfig::EffectV2{.instance_name = "Nop"};

  auto format = config.OutputFormat(loader_result.value().get());
  EXPECT_EQ(format.sample_format(), fuchsia::media::AudioSampleFormat::FLOAT);
  EXPECT_EQ(format.channels(), kOutputChannelsForEffect);
  EXPECT_EQ(format.frames_per_second(), PipelineConfig::kDefaultMixGroupRate);
}

}  // namespace media::audio

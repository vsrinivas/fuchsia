// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/processing/sinc_sampler.h"

#include <lib/syslog/cpp/macros.h>

#include <cstdint>
#include <utility>
#include <vector>

#include <ffl/string.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fidl/fuchsia.audio/cpp/wire_types.h"
#include "src/media/audio/lib/format2/channel_mapper.h"
#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/sample_converter.h"
#include "src/media/audio/lib/processing/filter.h"
#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/lib/timeline/timeline_rate.h"

namespace media_audio {
namespace {

using ::fuchsia_audio::SampleType;
using ::media::TimelineRate;
using ::testing::Each;
using ::testing::FloatEq;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Pointwise;

constexpr std::pair<uint32_t, uint32_t> kChannelConfigs[] = {
    {1, 1}, {1, 2}, {1, 3}, {1, 4}, {2, 1}, {2, 2}, {2, 3},
    {2, 4}, {3, 1}, {3, 2}, {3, 3}, {4, 1}, {4, 2}, {4, 4},
};

constexpr uint32_t kFrameRates[] = {
    8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000, 176400, 192000,
};

constexpr SampleType kSampleTypes[] = {
    SampleType::kUint8,
    SampleType::kInt16,
    SampleType::kInt32,
    SampleType::kFloat32,
};

Format CreateFormat(int64_t channel_count, int64_t frame_rate, SampleType sample_type) {
  return Format::CreateOrDie({sample_type, channel_count, frame_rate});
}

TEST(SincSamplerTest, CreateWithValidConfigs) {
  for (const auto& [source_channel_count, dest_channel_count] : kChannelConfigs) {
    for (const auto& source_frame_rate : kFrameRates) {
      for (const auto& dest_frame_rate : kFrameRates) {
        for (const auto& sample_type : kSampleTypes) {
          EXPECT_THAT(SincSampler::Create(
                          CreateFormat(source_channel_count, source_frame_rate, sample_type),
                          CreateFormat(dest_channel_count, dest_frame_rate, SampleType::kFloat32)),
                      NotNull());
        }
      }
    }
  }
}

TEST(SincSamplerTest, CreateFailsWithUnsupportedChannelConfigs) {
  const std::pair<uint32_t, uint32_t> unsupported_channel_configs[] = {
      {1, 5}, {1, 8}, {1, 9}, {2, 5}, {2, 8}, {2, 9}, {3, 5},
      {3, 8}, {3, 9}, {4, 5}, {4, 7}, {4, 9}, {5, 1}, {9, 1},
  };
  for (const auto& [source_channel_count, dest_channel_count] : unsupported_channel_configs) {
    for (const auto& frame_rate : kFrameRates) {
      for (const auto& sample_type : kSampleTypes) {
        EXPECT_THAT(
            SincSampler::Create(CreateFormat(source_channel_count, frame_rate, sample_type),
                                CreateFormat(dest_channel_count, frame_rate, SampleType::kFloat32)),
            IsNull());
      }
    }
  }
}

TEST(SincSamplerTest, CreateFailsWithUnsupportedDestSampleFormats) {
  const uint32_t frame_rate = 44100;
  for (const auto& [source_channel_count, dest_channel_count] : kChannelConfigs) {
    for (const auto& source_sample_type : kSampleTypes) {
      for (const auto& dest_sample_type : kSampleTypes) {
        if (dest_sample_type != SampleType::kFloat32) {
          EXPECT_THAT(SincSampler::Create(
                          CreateFormat(source_channel_count, frame_rate, source_sample_type),
                          CreateFormat(dest_channel_count, frame_rate, dest_sample_type)),
                      IsNull());
        }
      }
    }
  }
}

TEST(SincSamplerTest, Process) {
  auto sampler = SincSampler::Create(CreateFormat(1, 48000, SampleType::kFloat32),
                                     CreateFormat(1, 48000, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  const int64_t dest_frame_count = 5;
  // Make sure to provide enough samples to compensate for the filter length.
  const int64_t source_frame_count = dest_frame_count + sampler->pos_filter_length().Floor();

  std::vector<float> source_samples(source_frame_count);
  for (int i = 0; i < source_frame_count; ++i) {
    source_samples[i] = static_cast<float>(i + 1);
  }
  std::vector<float> dest_samples(dest_frame_count, 1.0f);

  Fixed source_offset = Fixed(0);
  int64_t dest_offset = 0;

  // All source samples should be accumulated into destination samples as-is.
  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kUnity, .scale = kUnityGainScale},
                   /*accumulate=*/true);
  for (int i = 0; i < dest_frame_count; ++i) {
    EXPECT_FLOAT_EQ(dest_samples[i], source_samples[i] + 1.0f) << i;
  }
}

TEST(SincSamplerTest, ProcessDownSample) {
  const uint32_t kSourceFrameRate = 48000;
  const uint32_t kDestFrameRate = 44100;
  auto sampler = SincSampler::Create(CreateFormat(1, kSourceFrameRate, SampleType::kFloat32),
                                     CreateFormat(1, kDestFrameRate, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  const int64_t dest_frame_count = 512;
  std::vector<float> dest_samples(dest_frame_count, 0.0f);
  int64_t dest_offset = 0;

  const int64_t source_frame_count = dest_frame_count / 2;
  const std::vector<float> source_samples(source_frame_count, 1.0f);
  Fixed source_offset = Fixed(0);

  sampler->state().ResetSourceStride(
      TimelineRate(Fixed(kSourceFrameRate).raw_value(), kDestFrameRate));

  // Process the first half of the destination.
  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kUnity, .scale = kUnityGainScale},
                   /*accumulate=*/false);
  EXPECT_GT(source_offset + sampler->pos_filter_length(), Fixed(source_frame_count));
  const int64_t first_half_dest_offset = dest_offset;

  // Now process the rest.
  source_offset -= Fixed(source_frame_count);
  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kUnity, .scale = kUnityGainScale},
                   /*accumulate=*/false);
  EXPECT_GT(source_offset + sampler->pos_filter_length(), Fixed(source_frame_count));

  // The "seam" between the source and destination samples should be invisible.
  const float kSeamThreshold = 0.001f;
  for (int64_t i = first_half_dest_offset - 2; i < first_half_dest_offset + 2; ++i) {
    EXPECT_NEAR(dest_samples[i], 1.0f, kSeamThreshold) << i;
  }
}

TEST(SincSamplerTest, ProcessUpSample) {
  const uint32_t kSourceFrameRate = 12000;
  const uint32_t kDestFrameRate = 48000;
  auto sampler = SincSampler::Create(CreateFormat(1, kSourceFrameRate, SampleType::kFloat32),
                                     CreateFormat(1, kDestFrameRate, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  const int64_t dest_frame_count = 1024;
  std::vector<float> dest_samples(dest_frame_count, 0.0f);
  int64_t dest_offset = 0;

  const int64_t source_frame_count = dest_frame_count / 8;
  const std::vector<float> source_samples(source_frame_count, 1.0f);
  Fixed source_offset = Fixed(0);

  sampler->state().ResetSourceStride(
      TimelineRate(Fixed(kSourceFrameRate).raw_value(), kDestFrameRate));

  // Process the first half of the destination.
  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kUnity, .scale = kUnityGainScale},
                   /*accumulate=*/false);
  EXPECT_GT(source_offset + sampler->pos_filter_length(), Fixed(source_frame_count));
  EXPECT_EQ(Fixed(source_offset * 4).Floor(), dest_offset);
  const int64_t first_half_dest_offset = dest_offset;

  // Now process the rest.
  source_offset -= Fixed(source_frame_count);
  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kUnity, .scale = kUnityGainScale},
                   /*accumulate=*/false);
  EXPECT_GT(source_offset + sampler->pos_filter_length(), Fixed(source_frame_count));

  // The "seam" between the source and destination samples should be invisible.
  const float kSeamThreshold = 0.001f;
  for (int64_t i = first_half_dest_offset - 2; i < first_half_dest_offset + 2; ++i) {
    EXPECT_NEAR(dest_samples[i], 1.0f, kSeamThreshold) << i;
  }
}

TEST(SincSamplerTest, ProcessWithConstantGain) {
  auto sampler = SincSampler::Create(CreateFormat(1, 48000, SampleType::kFloat32),
                                     CreateFormat(1, 48000, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  const int64_t dest_frame_count = 5;
  // Make sure to provide enough samples to compensate for the filter length.
  const int64_t source_frame_count = dest_frame_count + sampler->pos_filter_length().Floor();

  std::vector<float> source_samples(source_frame_count);
  for (int i = 0; i < source_frame_count; ++i) {
    source_samples[i] = static_cast<float>(i + 1);
  }
  std::vector<float> dest_samples(dest_frame_count, 1.0f);

  Fixed source_offset = Fixed(0);
  int64_t dest_offset = 0;

  // Source samples should be scaled with constant gain and accumulated into destination samples.
  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kNonUnity, .scale = 10.0f},
                   /*accumulate=*/true);
  for (int i = 0; i < dest_frame_count; ++i) {
    EXPECT_FLOAT_EQ(dest_samples[i], 10.0f * static_cast<float>(i + 1) + 1.0f) << i;
  }
}

TEST(SincSamplerTest, ProcessWithRampingGain) {
  auto sampler = SincSampler::Create(CreateFormat(1, 48000, SampleType::kFloat32),
                                     CreateFormat(1, 48000, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  const int64_t dest_frame_count = 5;
  // Make sure to provide enough samples to compensate for the filter length.
  const int64_t source_frame_count = dest_frame_count + sampler->pos_filter_length().Floor();

  std::vector<float> source_samples(source_frame_count);
  for (int i = 0; i < source_frame_count; ++i) {
    source_samples[i] = static_cast<float>(i + 1);
  }
  std::vector<float> dest_samples(dest_frame_count, 1.0f);

  Fixed source_offset = Fixed(0);
  int64_t dest_offset = 0;

  // Source samples should be scaled with ramping gain and accumulated into destination samples.
  const std::vector<float> scale_ramp = {2.0f, 4.0f, 6.0f, 8.0f, 10.0f};
  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kRamping, .scale_ramp = scale_ramp.data()},
                   /*accumulate=*/true);
  for (int i = 0; i < dest_frame_count; ++i) {
    EXPECT_FLOAT_EQ(dest_samples[i], scale_ramp[i] * static_cast<float>(i + 1) + 1.0f) << i;
  }
}

TEST(SincSamplerTest, ProcessWithSilentGain) {
  auto sampler = SincSampler::Create(CreateFormat(1, 48000, SampleType::kFloat32),
                                     CreateFormat(1, 48000, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  const int64_t dest_frame_count = 5;
  // Make sure to provide enough samples to compensate for the filter length.
  const int64_t source_frame_count = dest_frame_count + sampler->pos_filter_length().Floor();

  std::vector<float> source_samples(source_frame_count);
  for (int i = 0; i < source_frame_count; ++i) {
    source_samples[i] = static_cast<float>(i + 1);
  }
  std::vector<float> dest_samples(dest_frame_count, 1.0f);

  Fixed source_offset = Fixed(0);
  int64_t dest_offset = 0;

  // Nothing should be accumulated into destination samples when gain is silent.
  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kSilent, .scale = kMinGainScale},
                   /*accumulate=*/true);
  EXPECT_THAT(dest_samples, Each(1.0f));

  // If no accumulation, destination samples should be filled with zeros.
  source_offset = Fixed(0);
  dest_offset = 0;
  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kSilent, .scale = kMinGainScale},
                   /*accumulate=*/false);
  EXPECT_THAT(dest_samples, Each(0.0f));
}

TEST(SincSamplerTest, ProcessWithSourceOffsetAtFrameBoundary) {
  auto sampler = SincSampler::Create(CreateFormat(1, 44100, SampleType::kFloat32),
                                     CreateFormat(1, 44100, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  // Source offset is 46 of 50 frames, destination offset is 1 of 10, which should advance by 4.
  const int64_t dest_frame_count = 10;
  std::vector<float> dest_samples(dest_frame_count);
  int64_t dest_offset = 1;

  const int64_t source_frame_count = 50;
  std::vector<float> source_samples(source_frame_count);
  Fixed source_offset =
      Fixed(source_frame_count - 4) - sampler->pos_filter_length() + Fixed::FromRaw(1);

  const int64_t expected_advance = 4;
  const Fixed expected_source_offset = source_offset + Fixed(expected_advance);
  const int64_t expected_dest_offset = dest_offset + expected_advance;

  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kSilent, .scale = kMinGainScale},
                   /*accumulate=*/false);
  EXPECT_EQ(dest_offset, expected_dest_offset);
  EXPECT_EQ(source_offset, expected_source_offset) << ffl::String::DecRational << source_offset;
}

TEST(SincSamplerTest, ProcessWithSourceOffsetJustBeforeFrameBoundary) {
  auto sampler = SincSampler::Create(CreateFormat(1, 44100, SampleType::kFloat32),
                                     CreateFormat(1, 44100, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  // Source offset is 45.99 of 50 frames, destination offset is 1 of 10, which should advance by 5.
  const int64_t dest_frame_count = 10;
  std::vector<float> dest_samples(dest_frame_count);
  int64_t dest_offset = 1;

  const int64_t source_frame_count = 50;
  std::vector<float> source_samples(source_frame_count);
  Fixed source_offset = Fixed(source_frame_count - 4) - sampler->pos_filter_length();

  const int64_t expected_advance = 5;
  const Fixed expected_source_offset = source_offset + Fixed(expected_advance);
  const int64_t expected_dest_offset = dest_offset + expected_advance;

  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kSilent, .scale = kMinGainScale},
                   /*accumulate=*/false);
  EXPECT_EQ(dest_offset, expected_dest_offset);
  EXPECT_EQ(source_offset, expected_source_offset) << ffl::String::DecRational << source_offset;
}

TEST(SincSamplerTest, ProcessWithSourceOffsetAtEnd) {
  auto sampler = SincSampler::Create(CreateFormat(1, 44100, SampleType::kFloat32),
                                     CreateFormat(1, 44100, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  // Source offset is at the end already, destination offset is 0 of 50, which should not advance.
  const int64_t dest_frame_count = 50;
  std::vector<float> dest_samples(dest_frame_count);
  int64_t dest_offset = 0;

  const int64_t source_frame_count = 50;
  std::vector<float> source_samples(source_frame_count);
  Fixed source_offset =
      Fixed(source_frame_count) - sampler->pos_filter_length() + Fixed::FromRaw(1);
  const Fixed expected_source_offset = source_offset;

  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kSilent, .scale = kMinGainScale},
                   /*accumulate=*/false);
  EXPECT_EQ(dest_offset, 0);
  EXPECT_EQ(source_offset, expected_source_offset) << ffl::String::DecRational << source_offset;
}

TEST(SincSamplerTest, ProcessWithZeroStepSizeModuloNoRollover) {
  auto sampler = SincSampler::Create(CreateFormat(1, 44100, SampleType::kFloat32),
                                     CreateFormat(1, 44100, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  auto& state = sampler->state();
  state.ResetSourceStride(TimelineRate(Fixed(10000).raw_value() + 3333, 10000));
  EXPECT_EQ(state.step_size(), kOneFrame);
  EXPECT_EQ(state.step_size_modulo(), 3333ul);
  EXPECT_EQ(state.step_size_denominator(), 10000ul);

  const int64_t dest_frame_count = 3;
  std::vector<float> dest_samples(dest_frame_count);
  int64_t dest_offset = 0;

  const int64_t source_frame_count = 50;
  std::vector<float> source_samples(source_frame_count);
  Fixed source_offset = Fixed(0);

  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kSilent, .scale = kMinGainScale},
                   /*accumulate=*/false);
  EXPECT_EQ(dest_offset, dest_frame_count);
  EXPECT_EQ(source_offset, Fixed(3)) << ffl::String::DecRational << source_offset;
  EXPECT_EQ(state.source_pos_modulo(), 9999u);
}

TEST(SincSamplerTest, ProcessWithZeroStepSizeModuloWithRollover) {
  auto sampler = SincSampler::Create(CreateFormat(1, 44100, SampleType::kFloat32),
                                     CreateFormat(1, 44100, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  auto& state = sampler->state();
  state.ResetSourceStride(TimelineRate(Fixed(10000).raw_value() + 5000, 10000));
  EXPECT_EQ(state.step_size(), kOneFrame);
  EXPECT_EQ(state.step_size_modulo(), 1ul);
  EXPECT_EQ(state.step_size_denominator(), 2ul);

  const int64_t dest_frame_count = 3;
  std::vector<float> dest_samples(dest_frame_count);
  int64_t dest_offset = 1;

  const int64_t source_frame_count = 50;
  std::vector<float> source_samples(source_frame_count);
  Fixed source_offset = kOneFrame - Fixed::FromRaw(1);

  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kSilent, .scale = kMinGainScale},
                   /*accumulate=*/false);
  EXPECT_EQ(dest_offset, dest_frame_count);
  EXPECT_EQ(source_offset, Fixed(3)) << ffl::String::DecRational << source_offset;
  EXPECT_EQ(state.source_pos_modulo(), 0u);
}

TEST(SincSamplerTest, ProcessWithNonZeroStepSizeModuloNoRollover) {
  auto sampler = SincSampler::Create(CreateFormat(1, 44100, SampleType::kFloat32),
                                     CreateFormat(1, 44100, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  auto& state = sampler->state();
  state.ResetSourceStride(TimelineRate(Fixed(10000).raw_value() + 3331, 10000));
  EXPECT_EQ(state.step_size(), kOneFrame);
  EXPECT_EQ(state.step_size_modulo(), 3331ul);
  EXPECT_EQ(state.step_size_denominator(), 10000ul);
  state.set_source_pos_modulo(6);

  const int64_t dest_frame_count = 3;
  std::vector<float> dest_samples(dest_frame_count);
  int64_t dest_offset = 0;

  const int64_t source_frame_count = 50;
  std::vector<float> source_samples(source_frame_count);
  Fixed source_offset = Fixed(0);

  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kSilent, .scale = kMinGainScale},
                   /*accumulate=*/false);
  EXPECT_EQ(dest_offset, dest_frame_count);
  EXPECT_EQ(source_offset, Fixed(3)) << ffl::String::DecRational << source_offset;
  EXPECT_EQ(state.source_pos_modulo(), 9999u);
}

TEST(SincSamplerTest, ProcessWithNonZeroStepSizeModuloRollover) {
  auto sampler = SincSampler::Create(CreateFormat(1, 44100, SampleType::kFloat32),
                                     CreateFormat(1, 44100, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  auto& state = sampler->state();
  state.ResetSourceStride(TimelineRate(Fixed(10000).raw_value() + 3331, 10000));
  EXPECT_EQ(state.step_size(), kOneFrame);
  EXPECT_EQ(state.step_size_modulo(), 3331ul);
  EXPECT_EQ(state.step_size_denominator(), 10000ul);
  state.set_source_pos_modulo(3338);

  const int64_t dest_frame_count = 3;
  std::vector<float> dest_samples(dest_frame_count);
  int64_t dest_offset = 1;

  const int64_t source_frame_count = 50;
  std::vector<float> source_samples(source_frame_count);
  Fixed source_offset = Fixed(1) - Fixed::FromRaw(1);

  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kSilent, .scale = kMinGainScale},
                   /*accumulate=*/false);
  EXPECT_EQ(dest_offset, dest_frame_count);
  EXPECT_EQ(source_offset, Fixed(3)) << ffl::String::DecRational << source_offset;
  EXPECT_EQ(state.source_pos_modulo(), 0u);
}

TEST(SincSamplerTest, ProcessWithSourcePostModuloExactRollover) {
  auto sampler = SincSampler::Create(CreateFormat(1, 44100, SampleType::kFloat32),
                                     CreateFormat(1, 44100, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  auto& state = sampler->state();
  state.ResetSourceStride(TimelineRate(Fixed(3).raw_value() - 1, 3));
  EXPECT_EQ(state.step_size(), kOneFrame - Fixed::FromRaw(1));
  EXPECT_EQ(state.step_size_modulo(), 2ul);
  EXPECT_EQ(state.step_size_denominator(), 3ul);
  state.set_source_pos_modulo(2);

  const int64_t dest_frame_count = 3;
  std::vector<float> dest_samples(dest_frame_count);
  int64_t dest_offset = 0;

  const int64_t source_frame_count = 10;
  std::vector<float> source_samples(source_frame_count);
  Fixed source_offset =
      Fixed(source_frame_count - 2) - sampler->pos_filter_length() + Fixed::FromRaw(1);

  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kSilent, .scale = kMinGainScale},
                   /*accumulate=*/false);
  EXPECT_EQ(dest_offset, 2);
  EXPECT_EQ(source_offset,
            Fixed(Fixed(source_frame_count) - sampler->pos_filter_length() + Fixed::FromRaw(1)))
      << ffl::String::DecRational << source_offset;
  EXPECT_EQ(state.source_pos_modulo(), 0u);
}

class SincSamplerOutputTest : public testing::Test {
 protected:
  // Based on an arbitrary near-zero source position (-1/128), with a sinc curve for unity rate
  // conversion, we use data values calculated so that if these first 13 values (the filter's
  // negative wing) are ignored, we expect a generated output value of
  // `kValueWithoutPreviousFrames`. If they are NOT ignored, then we expect the result
  // `kValueWithPreviousFrames`.
  static constexpr float kSource[] = {
      1330.10897f, -1330.10897f, 1330.10897f, -1330.10897f, 1330.10897f, -1330.10897f,
      1330.10897f, -1330.10897f, 1330.10897f, -1330.10897f, 1330.10897f, -1330.10897f,
      1330.10897f,  // ... source frames to satisfy negative filter length.
      -10.001010f,  // Center source frame
      268.88298f,   // Source frames to satisfy positive filter length ...
      -268.88298f, 268.88298f,   -268.88298f, 268.88298f,   -268.88298f, 268.88298f,
      -268.88298f, 268.88298f,   -268.88298f, 268.88298f,   -268.88298f, 268.88298f,
  };
  static constexpr Fixed kProcessOneFrameSourceOffset = ffl::FromRatio(1, 128);
  // The center frame should contribute -10.0, the positive wing -5.0, and the negative wing +25.0.
  static constexpr float kValueWithoutPreviousFrames = -15.0;
  static constexpr float kValueWithPreviousFrames = 10.0;

  // Processes a single frame of output based on `kSource[0]`.
  static float ProcessOneFrame(Sampler& sampler, Fixed source_offset) {
    auto neg_length = sampler.neg_filter_length().Floor();
    auto pos_length = sampler.pos_filter_length().Floor();
    EXPECT_NE(Fixed(pos_length).raw_value(), sampler.neg_filter_length().raw_value() - 1)
        << "This test assumes SincSampler is symmetric, and that negative width includes a "
           "fraction";

    float dest_sample = 0.0f;
    int64_t dest_offset = 0;

    sampler.Process(Sampler::Source{&kSource[neg_length - 1], &source_offset, pos_length},
                    Sampler::Dest{&dest_sample, &dest_offset, 1}, {}, false);
    EXPECT_EQ(dest_offset, 1u) << "No output frame was produced";

    FX_LOGS(INFO) << "Coefficients " << std::setprecision(12) << kSource[12] << " " << kSource[13]
                  << " " << kSource[14] << ", value " << dest_sample;

    return dest_sample;
  }

  // Tests sampler with a passthrough process of a given `source_samples` with a given
  // `channel_count` and `source_sample_type`.
  template <typename SourceSampleType>
  static void TestPassthrough(uint32_t channel_count, SampleType source_sample_type,
                              const std::vector<SourceSampleType>& source_samples) {
    // Create sampler.
    auto sampler = SincSampler::Create(CreateFormat(channel_count, 48000, source_sample_type),
                                       CreateFormat(channel_count, 48000, SampleType::kFloat32));
    ASSERT_THAT(sampler, NotNull());
    EXPECT_EQ(sampler->pos_filter_length().raw_value(), SincFilter::kFracSideLength);
    EXPECT_EQ(sampler->neg_filter_length().raw_value(), SincFilter::kFracSideLength);

    // Process samples with unity gain.
    const int64_t dest_frame_count = static_cast<int64_t>(source_samples.size() / channel_count);
    // Make sure to provide enough samples to compensate for the filter length.
    const int64_t source_frame_count = dest_frame_count + sampler->pos_filter_length().Floor();
    auto padded_source_samples = source_samples;
    padded_source_samples.insert(padded_source_samples.end(),
                                 channel_count * sampler->pos_filter_length().Floor(), 0.0f);

    Fixed source_offset = Fixed();
    std::vector<float> dest_samples(source_samples.size(), 0.0f);
    int64_t dest_offset = 0;

    sampler->Process({padded_source_samples.data(), &source_offset, source_frame_count},
                     {dest_samples.data(), &dest_offset, dest_frame_count},
                     {.type = GainType::kUnity, .scale = kUnityGainScale},
                     /*accumulate=*/false);
    EXPECT_EQ(dest_offset, dest_frame_count);
    EXPECT_EQ((source_offset), Fixed(dest_frame_count));
    for (int i = 0; i < dest_frame_count; ++i) {
      EXPECT_FLOAT_EQ(SampleConverter<SourceSampleType>::ToFloat(source_samples[i]),
                      dest_samples[i])
          << i;
    }
  }

  // Tests sampler with a rechannelization process of a given `source_samples` against
  // `expected_dest_samples` for a given `SourceChannelCount` and `DestChannelCount`.
  template <uint32_t SourceChannelCount, uint32_t DestChannelCount>
  static void TestRechannelization(const std::vector<float>& source_samples,
                                   const std::vector<float>& expected_dest_samples) {
    // Create sampler.
    auto sampler =
        SincSampler::Create(CreateFormat(SourceChannelCount, 48000, SampleType::kFloat32),
                            CreateFormat(DestChannelCount, 48000, SampleType::kFloat32));
    EXPECT_EQ(sampler->pos_filter_length().raw_value(), SincFilter::kFracSideLength);
    EXPECT_EQ(sampler->neg_filter_length().raw_value(), SincFilter::kFracSideLength);

    // Process samples with unity gain.
    const int64_t dest_frame_count =
        static_cast<int64_t>(source_samples.size() / SourceChannelCount);
    ASSERT_EQ(dest_frame_count * DestChannelCount,
              static_cast<int64_t>(expected_dest_samples.size()));
    // Make sure to provide enough samples to compensate for the filter length.
    const int64_t source_frame_count = dest_frame_count + sampler->pos_filter_length().Floor();
    auto padded_source_samples = source_samples;
    padded_source_samples.insert(padded_source_samples.end(),
                                 SourceChannelCount * sampler->pos_filter_length().Floor(), 0.0f);

    Fixed source_offset = Fixed(0);
    std::vector<float> dest_samples(expected_dest_samples.size(), 0.0f);
    int64_t dest_offset = 0;

    sampler->Process({padded_source_samples.data(), &source_offset, source_frame_count},
                     {dest_samples.data(), &dest_offset, dest_frame_count},
                     {.type = GainType::kUnity, .scale = kUnityGainScale},
                     /*accumulate=*/false);
    EXPECT_EQ(dest_offset, dest_frame_count);
    EXPECT_EQ((source_offset), Fixed(dest_frame_count));
    EXPECT_THAT(dest_samples, Pointwise(FloatEq(), expected_dest_samples));
  }
};

TEST_F(SincSamplerOutputTest, ProcessOneNoCache) {
  auto sampler = SincSampler::Create(CreateFormat(1, 44100, SampleType::kFloat32),
                                     CreateFormat(1, 44100, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  // Process a single frame. We use a slightly non-zero position because at true 0, only the sample
  // (not the positive or negative wings) are used. In this case we not provided previous frames.
  const float dest_sample = ProcessOneFrame(*sampler, -kProcessOneFrameSourceOffset);

  // If we incorrectly shifted/retained even a single frame of the above data, this won't match.
  EXPECT_FLOAT_EQ(dest_sample, kValueWithoutPreviousFrames) << std::setprecision(12) << dest_sample;
}

TEST_F(SincSamplerOutputTest, ProcessOneWithCache) {
  auto sampler = SincSampler::Create(CreateFormat(1, 44100, SampleType::kFloat32),
                                     CreateFormat(1, 44100, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());
  auto neg_length = sampler->neg_filter_length().Floor();

  // Now, populate the cache with previous frames, instead of using default (silence) values.
  // The output value of `source_offset` tells us the cache is populated with `neg_length - 1`
  // frames, which is ideal for sampling a subsequent source buffer starting at source position 0.
  float dest_sample = 0.0f;
  int64_t dest_offset = 0;
  const auto source_frame_count = neg_length - 1;
  Fixed source_offset = Fixed(source_frame_count) - kProcessOneFrameSourceOffset;

  sampler->Process(Sampler::Source{&kSource[0], &source_offset, source_frame_count},
                   Sampler::Dest{&dest_sample, &dest_offset, 1}, {}, false);
  EXPECT_EQ(source_offset, Fixed(source_frame_count) - kProcessOneFrameSourceOffset);
  EXPECT_EQ(dest_offset, 0u) << "Unexpectedly produced output " << dest_sample;

  // Process a single frame. We use a slightly non-zero position because at true 0, only the sample
  // itself (not positive or negative widths) are needed. In this case we provide previous frames.
  dest_sample = ProcessOneFrame(*sampler, -kProcessOneFrameSourceOffset);

  // If we incorrectly shifted/retained even a single frame of the above data, this won't match.
  EXPECT_FLOAT_EQ(dest_sample, kValueWithPreviousFrames) << std::setprecision(12) << dest_sample;
}

TEST_F(SincSamplerOutputTest, ProcessFrameByFrameCached) {
  auto sampler = SincSampler::Create(CreateFormat(1, 44100, SampleType::kFloat32),
                                     CreateFormat(1, 44100, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());
  auto neg_length = sampler->neg_filter_length().Floor();

  // Now, populate the cache with previous data, one frame at a time.
  float dest_sample = 0.0f;
  int64_t dest_offset = 0;
  const auto source_frame_count = 1;
  Fixed source_offset = Fixed(source_frame_count) - kProcessOneFrameSourceOffset;

  for (auto neg_idx = 0; neg_idx < neg_length - 1; ++neg_idx) {
    sampler->Process(Sampler::Source{&kSource[neg_idx], &source_offset, source_frame_count},
                     Sampler::Dest{&dest_sample, &dest_offset, 1}, {}, false);
    EXPECT_EQ(source_offset, Fixed(source_frame_count) - kProcessOneFrameSourceOffset);
    EXPECT_EQ(dest_offset, 0u) << "Unexpectedly produced output " << dest_sample;
  }

  // Process a single frame. We use a slightly non-zero position because at true 0, only the sample
  // itself (not positive or negative widths) are needed. In this case we provide previous frames.
  dest_sample = ProcessOneFrame(*sampler, -kProcessOneFrameSourceOffset);

  // If we incorrectly shifted/retained even a single frame of the above data, this won't match.
  EXPECT_FLOAT_EQ(dest_sample, kValueWithPreviousFrames) << std::setprecision(12) << dest_sample;
}

TEST_F(SincSamplerOutputTest, ProcessPassthroughUint8) {
  const std::vector<uint8_t> source_samples = {0x00, 0xFF, 0x27, 0xCD, 0x7F, 0x80, 0xA6, 0x6D};

  // Test mono.
  TestPassthrough<uint8_t>(/*channel_count=*/1, SampleType::kUint8, source_samples);

  // Test stereo.
  TestPassthrough<uint8_t>(/*channel_count=*/2, SampleType::kUint8, source_samples);

  // Test 4 channels.
  TestPassthrough<uint8_t>(/*channel_count=*/4, SampleType::kUint8, source_samples);
}

TEST_F(SincSamplerOutputTest, ProcessPassthroughInt16) {
  const std::vector<int16_t> source_samples = {-0x8000, 0x7FFF, -0x67A7, 0x4D4D,
                                               -0x123,  0,      0x2600,  -0x2DCB};

  // Test mono.
  TestPassthrough<int16_t>(/*channel_count=*/1, SampleType::kInt16, source_samples);

  // Test stereo.
  TestPassthrough<int16_t>(/*channel_count=*/2, SampleType::kInt16, source_samples);

  // Test 4 channels.
  TestPassthrough<int16_t>(/*channel_count=*/4, SampleType::kInt16, source_samples);
}

TEST_F(SincSamplerOutputTest, ProcessPassthroughInt24In32) {
  const std::vector<int32_t> source_samples = {kMinInt24In32, kMaxInt24In32, -0x67A7E700,
                                               0x4D4D4D00,    -0x1234500,    0,
                                               0x26006200,    -0x2DCBA900};

  // Test mono.
  TestPassthrough<int32_t>(/*channel_count=*/1, SampleType::kInt32, source_samples);

  // Test stereo.
  TestPassthrough<int32_t>(/*channel_count=*/2, SampleType::kInt32, source_samples);

  // Test 4 channels.
  TestPassthrough<int32_t>(/*channel_count=*/4, SampleType::kInt32, source_samples);
}

TEST_F(SincSamplerOutputTest, ProcessPassthroughFloat) {
  const std::vector<float> source_samples = {
      -1.0, 1.0f, -0.809783935f, 0.603912353f, -0.00888061523f, 0.0f, 0.296875f, -0.357757568f};

  // Test mono.
  TestPassthrough<float>(/*channel_count=*/1, SampleType::kFloat32, source_samples);

  // Test stereo.
  TestPassthrough<float>(/*channel_count=*/2, SampleType::kFloat32, source_samples);

  // Test 4 channels.
  TestPassthrough<float>(/*channel_count=*/4, SampleType::kFloat32, source_samples);
}

TEST_F(SincSamplerOutputTest, ProcessRechannelizationMono) {
  const std::vector<float> source_samples = {-1.0f, 1.0f, 0.3f};

  // Test mono to stereo.
  TestRechannelization<1, 2>(source_samples, {-1.0f, -1.0f, 1.0f, 1.0f, 0.3f, 0.3f});

  // Test mono to 3 channels.
  TestRechannelization<1, 3>(source_samples,
                             {-1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 0.3f, 0.3f, 0.3f});

  // Test mono to quad.
  TestRechannelization<1, 4>(
      source_samples, {-1.0f, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.3f, 0.3f, 0.3f, 0.3f});
}

TEST_F(SincSamplerOutputTest, ProcessRechannelizationStereo) {
  const std::vector<float> source_samples = {-1.0f, 1.0f, 0.3f, 0.1f};

  // Test stereo to mono.
  TestRechannelization<2, 1>(source_samples, {0.0f, 0.2f});

  // Test stereo to 3 channels.
  TestRechannelization<2, 3>(source_samples, {-1.0f, 1.0f, 0.0f, 0.3f, 0.1f, 0.2f});

  // Test stereo to quad.
  TestRechannelization<2, 4>(source_samples, {-1.0f, 1.0f, -1.0f, 1.0f, 0.3f, 0.1f, 0.3f, 0.1f});
}

TEST_F(SincSamplerOutputTest, ProcessRechannelizationQuad) {
  const std::vector<float> source_samples = {-1.0f, 0.8f, 1.0f, -0.8f, 0.1f, 0.3f, -0.3f, -0.9f};

  // Test quad to mono.
  if constexpr (kEnable4ChannelWorkaround) {
    TestRechannelization<4, 1>(source_samples, {-0.1f, 0.2f});
  } else {
    TestRechannelization<4, 1>(source_samples, {0.0f, -0.2f});
  }

  // Test quad to stereo.
  if constexpr (kEnable4ChannelWorkaround) {
    TestRechannelization<4, 2>(source_samples, {-1.0f, 0.8f, 0.1f, 0.3f});
  } else {
    TestRechannelization<4, 2>(source_samples, {0.0f, 0.0f, -0.1f, -0.3f});
  }
}

}  // namespace
}  // namespace media_audio

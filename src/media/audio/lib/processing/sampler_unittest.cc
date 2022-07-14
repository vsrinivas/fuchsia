// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/processing/sampler.h"

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fidl/fuchsia.mediastreams/cpp/wire_types.h"
#include "src/media/audio/lib/format2/format.h"
#include "src/media/audio/lib/processing/gain.h"

namespace media_audio {
namespace {

using ::fuchsia_mediastreams::wire::AudioSampleFormat;
using ::testing::IsNull;
using ::testing::NotNull;

TEST(SamplerTest, CreateWithUnityRate) {
  const Format source_format = Format::CreateOrDie({AudioSampleFormat::kSigned16, 1, 44100});
  const Format dest_format = Format::CreateOrDie({AudioSampleFormat::kFloat, 2, 44100});

  // Default should return a valid `PointSampler`.
  const auto default_sampler = Sampler::Create(source_format, dest_format);
  ASSERT_THAT(default_sampler, NotNull());
  EXPECT_EQ(default_sampler->type(), Sampler::Type::kPointSampler);

  // `kPointSampler` should return the same valid `PointSampler` as the default case.
  const auto point_sampler =
      Sampler::Create(source_format, dest_format, Sampler::Type::kPointSampler);
  ASSERT_THAT(point_sampler, NotNull());
  EXPECT_EQ(point_sampler->type(), Sampler::Type::kPointSampler);

  // `kSincSampler` should return a valid `SincSampler` although not optimal in practice.
  const auto sinc_sampler =
      Sampler::Create(source_format, dest_format, Sampler::Type::kSincSampler);
  ASSERT_THAT(sinc_sampler, NotNull());
  EXPECT_EQ(sinc_sampler->type(), Sampler::Type::kSincSampler);
}

TEST(SamplerTest, CreateWithNonUnityRate) {
  const Format source_format = Format::CreateOrDie({AudioSampleFormat::kFloat, 2, 8000});
  const Format dest_format = Format::CreateOrDie({AudioSampleFormat::kFloat, 1, 44100});

  // Default should return a valid `SincSampler`.
  const auto default_sampler = Sampler::Create(source_format, dest_format);
  ASSERT_THAT(default_sampler, NotNull());
  EXPECT_EQ(default_sampler->type(), Sampler::Type::kSincSampler);

  // `kPointSampler` should return `nullptr` since `PointSampler` is only supported for unity rates.
  const auto point_sampler =
      Sampler::Create(source_format, dest_format, Sampler::Type::kPointSampler);
  EXPECT_THAT(point_sampler, IsNull());

  // `kSincSampler` should return the same valid `SincSampler` as the default case.
  const auto sinc_sampler =
      Sampler::Create(source_format, dest_format, Sampler::Type::kSincSampler);
  EXPECT_THAT(sinc_sampler, NotNull());
  EXPECT_EQ(sinc_sampler->type(), Sampler::Type::kSincSampler);
}

TEST(SamplerTest, MixSampleSilent) {
  const std::vector<float> source_samples = {-0.5f, 0.25f, 1.0f, 2.0f};

  for (const float source_sample : source_samples) {
    const float scale = 0.5f * source_sample;

    float dest_sample = -0.1f;
    MixSample<GainType::kSilent, false>(source_sample, &dest_sample, scale);
    EXPECT_FLOAT_EQ(dest_sample, 0.0f);

    float dest_sample_to_accumulate = -0.2f;
    MixSample<GainType::kSilent, true>(source_sample, &dest_sample_to_accumulate, scale);
    EXPECT_FLOAT_EQ(dest_sample_to_accumulate, -0.2f);
  }
}

TEST(SamplerTest, MixSampleNonUnityOrRamping) {
  const std::vector<float> source_samples = {-0.5f, 0.25f, 1.0f, 2.0f};
  const std::vector<float> scales = {0.2f, 0.75f, 1.5f};

  const float kDestSampleValue = 0.4f;
  for (const float source_sample : source_samples) {
    for (const float scale : scales) {
      float dest_sample = kDestSampleValue;
      MixSample<GainType::kNonUnity, false>(source_sample, &dest_sample, scale);
      EXPECT_FLOAT_EQ(dest_sample, source_sample * scale);
      dest_sample = kDestSampleValue;
      MixSample<GainType::kRamping, false>(source_sample, &dest_sample, scale);
      EXPECT_FLOAT_EQ(dest_sample, source_sample * scale);

      float dest_sample_to_accumulate = kDestSampleValue;
      MixSample<GainType::kNonUnity, true>(source_sample, &dest_sample_to_accumulate, scale);
      EXPECT_FLOAT_EQ(dest_sample_to_accumulate, source_sample * scale + kDestSampleValue);
      dest_sample_to_accumulate = kDestSampleValue;
      MixSample<GainType::kRamping, true>(source_sample, &dest_sample_to_accumulate, scale);
      EXPECT_FLOAT_EQ(dest_sample_to_accumulate, source_sample * scale + kDestSampleValue);
    }
  }
}

TEST(SamplerTest, MixSampleUnity) {
  const std::vector<float> source_samples = {-0.5f, 0.25f, 1.0f, 2.0f};

  for (const float source_sample : source_samples) {
    const float scale = 0.5f * source_sample;

    float dest_sample = 0.5f;
    MixSample<GainType::kUnity, false>(source_sample, &dest_sample, kUnityGainScale);
    EXPECT_FLOAT_EQ(dest_sample, source_sample);

    float dest_sample_to_accumulate = 2.0f;
    MixSample<GainType::kUnity, true>(source_sample, &dest_sample_to_accumulate, scale);
    EXPECT_FLOAT_EQ(dest_sample_to_accumulate, source_sample + 2.0f);
  }
}

}  // namespace
}  // namespace media_audio

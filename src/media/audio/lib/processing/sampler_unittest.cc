// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/processing/sampler.h"

#include <vector>

#include <gtest/gtest.h>

#include "src/media/audio/lib/processing/gain.h"

namespace media_audio {
namespace {

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

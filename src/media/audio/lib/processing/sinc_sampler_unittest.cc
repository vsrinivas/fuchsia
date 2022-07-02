// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/processing/sinc_sampler.h"

#include <cstdint>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ffl/fixed.h"
#include "fidl/fuchsia.mediastreams/cpp/wire_types.h"
#include "lib/syslog/cpp/macros.h"
#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/processing/gain.h"

namespace media_audio {
namespace {

using ::fuchsia_mediastreams::wire::AudioSampleFormat;
using ::testing::Each;
using ::testing::NotNull;

constexpr std::pair<uint32_t, uint32_t> kChannelConfigs[] = {
    {1, 1}, {1, 2}, {1, 3}, {1, 4}, {2, 1}, {2, 2}, {2, 3},
    {2, 4}, {3, 1}, {3, 2}, {3, 3}, {4, 1}, {4, 2}, {4, 4},
};

constexpr uint32_t kFrameRates[] = {
    8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000, 176400, 192000,
};

constexpr AudioSampleFormat kSampleFormats[] = {
    AudioSampleFormat::kUnsigned8,
    AudioSampleFormat::kSigned16,
    AudioSampleFormat::kSigned24In32,
    AudioSampleFormat::kFloat,
};

Format CreateFormat(uint32_t channel_count, uint32_t frame_rate, AudioSampleFormat sample_format) {
  return Format::CreateOrDie({sample_format, channel_count, frame_rate});
}

TEST(SincSamplerTest, CreateWithValidConfigs) {
  for (const auto& [source_channel_count, dest_channel_count] : kChannelConfigs) {
    for (const auto& source_frame_rate : kFrameRates) {
      for (const auto& dest_frame_rate : kFrameRates) {
        for (const auto& sample_format : kSampleFormats) {
          EXPECT_THAT(
              SincSampler::Create(
                  CreateFormat(source_channel_count, source_frame_rate, sample_format),
                  CreateFormat(dest_channel_count, dest_frame_rate, AudioSampleFormat::kFloat)),
              NotNull());
        }
      }
    }
  }
}

TEST(SincSamplerTest, ProcessSilentGain) {
  auto sampler = SincSampler::Create(CreateFormat(1, 48000, AudioSampleFormat::kFloat),
                                     CreateFormat(1, 48000, AudioSampleFormat::kFloat));
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

  const Sampler::Gain gain = {.type = GainType::kSilent, .scale = kMinGainScale};

  // Process with silent gain with accumulation, `dest_samples` should remain as-is.
  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count}, gain,
                   /*accumulate=*/true);
  EXPECT_THAT(dest_samples, Each(1.0f));

  // Reset offsets and process with silent gain again, but this time with no accumulation, which
  // should fill `dest_samples` with all zeros now.
  source_offset = Fixed(0);
  dest_offset = 0;
  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count}, gain,
                   /*accumulate=*/false);
  EXPECT_THAT(dest_samples, Each(0.0f));
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
};

TEST_F(SincSamplerOutputTest, ProcessOneNoCache) {
  auto sampler = SincSampler::Create(CreateFormat(1, 44100, AudioSampleFormat::kFloat),
                                     CreateFormat(1, 44100, AudioSampleFormat::kFloat));
  ASSERT_THAT(sampler, NotNull());

  // Process a single frame. We use a slightly non-zero position because at true 0, only the sample
  // (not the positive or negative wings) are used. In this case we not provided previous frames.
  const float dest_sample = ProcessOneFrame(*sampler, -kProcessOneFrameSourceOffset);

  // If we incorrectly shifted/retained even a single frame of the above data, this won't match.
  EXPECT_FLOAT_EQ(dest_sample, kValueWithoutPreviousFrames) << std::setprecision(12) << dest_sample;
}

TEST_F(SincSamplerOutputTest, ProcessOneWithCache) {
  auto sampler = SincSampler::Create(CreateFormat(1, 44100, AudioSampleFormat::kFloat),
                                     CreateFormat(1, 44100, AudioSampleFormat::kFloat));
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
  auto sampler = SincSampler::Create(CreateFormat(1, 44100, AudioSampleFormat::kFloat),
                                     CreateFormat(1, 44100, AudioSampleFormat::kFloat));
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

// TODO(fxbug.dev/87651): Move the rest of the `media::audio::mixer::SincSampler` unit tests once
// `media::audio::mixer::Mixer` code is fully migrated.

}  // namespace
}  // namespace media_audio

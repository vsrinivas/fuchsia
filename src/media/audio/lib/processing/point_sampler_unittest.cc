// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/processing/point_sampler.h"

#include <cstdint>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "ffl/fixed.h"
#include "fidl/fuchsia.mediastreams/cpp/wire_types.h"
#include "src/media/audio/lib/format2/fixed.h"
#include "src/media/audio/lib/format2/sample_converter.h"
#include "src/media/audio/lib/processing/gain.h"

namespace media_audio {
namespace {

using ::fuchsia_mediastreams::wire::AudioSampleFormat;
using ::testing::IsNull;
using ::testing::NotNull;

constexpr std::pair<uint32_t, uint32_t> kChannelConfigs[] = {
    {1, 1}, {1, 2}, {1, 3}, {1, 4}, {2, 1}, {2, 2}, {2, 3}, {2, 4}, {3, 1},
    {3, 2}, {3, 3}, {4, 1}, {4, 2}, {4, 4}, {5, 5}, {6, 6}, {7, 7}, {8, 8},
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

TEST(PointSamplerTest, CreateWithValidConfigs) {
  for (const auto& [source_channel_count, dest_channel_count] : kChannelConfigs) {
    for (const auto& frame_rate : kFrameRates) {
      for (const auto& sample_format : kSampleFormats) {
        EXPECT_THAT(PointSampler::Create(
                        CreateFormat(source_channel_count, frame_rate, sample_format),
                        CreateFormat(dest_channel_count, frame_rate, AudioSampleFormat::kFloat)),
                    NotNull());
      }
    }
  }
}

TEST(PointSamplerTest, CreateFailsWithMismatchingFrameRates) {
  const AudioSampleFormat sample_format = AudioSampleFormat::kFloat;
  for (const auto& [source_channel_count, dest_channel_count] : kChannelConfigs) {
    for (const auto& source_frame_rate : kFrameRates) {
      for (const auto& dest_frame_rate : kFrameRates) {
        if (source_frame_rate != dest_frame_rate) {
          EXPECT_THAT(PointSampler::Create(
                          CreateFormat(source_channel_count, source_frame_rate, sample_format),
                          CreateFormat(dest_channel_count, dest_frame_rate, sample_format)),
                      IsNull());
        }
      }
    }
  }
}

TEST(PointSamplerTest, CreateFailsWithUnsupportedChannelConfigs) {
  const std::pair<uint32_t, uint32_t> unsupported_channel_configs[] = {
      {1, 5}, {1, 8}, {1, 9}, {2, 5}, {2, 8}, {2, 9}, {3, 5},
      {3, 8}, {3, 9}, {4, 5}, {4, 7}, {4, 9}, {5, 1}, {9, 1},
  };
  for (const auto& [source_channel_count, dest_channel_count] : unsupported_channel_configs) {
    for (const auto& frame_rate : kFrameRates) {
      for (const auto& sample_format : kSampleFormats) {
        EXPECT_THAT(PointSampler::Create(
                        CreateFormat(source_channel_count, frame_rate, sample_format),
                        CreateFormat(dest_channel_count, frame_rate, AudioSampleFormat::kFloat)),
                    IsNull());
      }
    }
  }
}

TEST(PointSamplerTest, CreateFailsWithUnsupportedDestSampleFormats) {
  const uint32_t frame_rate = 44100;
  for (const auto& [source_channel_count, dest_channel_count] : kChannelConfigs) {
    for (const auto& source_sample_format : kSampleFormats) {
      for (const auto& dest_sample_format : kSampleFormats) {
        if (dest_sample_format != fuchsia_mediastreams::AudioSampleFormat::kFloat) {
          EXPECT_THAT(PointSampler::Create(
                          CreateFormat(source_channel_count, frame_rate, source_sample_format),
                          CreateFormat(dest_channel_count, frame_rate, dest_sample_format)),
                      IsNull());
        }
      }
    }
  }
}

class PassthroughTest : public ::testing::TestWithParam<Fixed> {};

TEST_P(PassthroughTest, PassthroughMono) {
  // Create mono sampler.
  auto mono_sampler = PointSampler::Create(CreateFormat(1, 48000, AudioSampleFormat::kUnsigned8),
                                           CreateFormat(1, 48000, AudioSampleFormat::kFloat));

  // Process with unity gain.
  const std::vector<uint8_t> source_samples = {0x00, 0xFF, 0x27, 0xCD, 0x7F, 0x80, 0xA6, 0x6D};
  const int64_t frame_count = static_cast<int64_t>(source_samples.size());

  Fixed source_offset = GetParam();
  Sampler::Source source = {source_samples.data(), &source_offset, frame_count};

  Sampler::Gain gain = {.type = GainType::kUnity, .scale = kUnityGainScale};

  std::vector<float> dest_samples(source_samples.size(), 0.0f);
  int64_t dest_offset = 0;
  Sampler::Dest dest = {dest_samples.data(), &dest_offset, frame_count};

  mono_sampler->Process(source, dest, gain, /*accumulate=*/false);
  for (int i = 0; i < frame_count; ++i) {
    EXPECT_FLOAT_EQ(SampleConverter<uint8_t>::ToFloat(source_samples[i]), dest_samples[i]);
  }
}

INSTANTIATE_TEST_SUITE_P(PointSamplerTest, PassthroughTest,
                         testing::Values(-kHalfFrame, Fixed(0),
                                         ffl::FromRaw<kPtsFractionalBits>(kHalfFrame.raw_value() -
                                                                          1)));

// TODO(fxbug.dev/87651): Move the rest of the `media::audio::mixer::PointSampler` unit tests once
// `media::audio::mixer::Mixer` code is fully migrated.

}  // namespace
}  // namespace media_audio

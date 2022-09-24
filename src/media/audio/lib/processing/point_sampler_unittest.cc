// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/processing/point_sampler.h"

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
#include "src/media/audio/lib/processing/gain.h"

namespace media_audio {
namespace {

using ::fuchsia_audio::SampleType;
using ::testing::Each;
using ::testing::FloatEq;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::Pointwise;

constexpr std::pair<uint32_t, uint32_t> kChannelConfigs[] = {
    {1, 1}, {1, 2}, {1, 3}, {1, 4}, {2, 1}, {2, 2}, {2, 3}, {2, 4}, {3, 1},
    {3, 2}, {3, 3}, {4, 1}, {4, 2}, {4, 4}, {5, 5}, {6, 6}, {7, 7}, {8, 8},
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

Format CreateFormat(uint32_t channel_count, uint32_t frame_rate, SampleType sample_format) {
  return Format::CreateOrDie({sample_format, channel_count, frame_rate});
}

TEST(PointSamplerTest, CreateWithValidConfigs) {
  for (const auto& [source_channel_count, dest_channel_count] : kChannelConfigs) {
    for (const auto& frame_rate : kFrameRates) {
      for (const auto& sample_format : kSampleTypes) {
        EXPECT_THAT(PointSampler::Create(
                        CreateFormat(source_channel_count, frame_rate, sample_format),
                        CreateFormat(dest_channel_count, frame_rate, SampleType::kFloat32)),
                    NotNull());
      }
    }
  }
}

TEST(PointSamplerTest, CreateFailsWithMismatchingFrameRates) {
  const SampleType sample_format = SampleType::kFloat32;
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
      for (const auto& sample_format : kSampleTypes) {
        EXPECT_THAT(PointSampler::Create(
                        CreateFormat(source_channel_count, frame_rate, sample_format),
                        CreateFormat(dest_channel_count, frame_rate, SampleType::kFloat32)),
                    IsNull());
      }
    }
  }
}

TEST(PointSamplerTest, CreateFailsWithUnsupportedDestSampleFormats) {
  const uint32_t frame_rate = 44100;
  for (const auto& [source_channel_count, dest_channel_count] : kChannelConfigs) {
    for (const auto& source_sample_format : kSampleTypes) {
      for (const auto& dest_sample_format : kSampleTypes) {
        if (dest_sample_format != SampleType::kFloat32) {
          EXPECT_THAT(PointSampler::Create(
                          CreateFormat(source_channel_count, frame_rate, source_sample_format),
                          CreateFormat(dest_channel_count, frame_rate, dest_sample_format)),
                      IsNull());
        }
      }
    }
  }
}

TEST(PointSamplerTest, Process) {
  // Create sampler.
  auto sampler = PointSampler::Create(CreateFormat(1, 48000, SampleType::kFloat32),
                                      CreateFormat(1, 48000, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  const std::vector<float> source_samples = {0.1f, -0.2f, 0.3f, -0.4f, 0.5f};
  const int64_t source_frame_count = static_cast<int64_t>(source_samples.size());
  Fixed source_offset = Fixed(0);

  // Start with existing samples to accumulate.
  std::vector<float> dest_samples(5, 1.0f);
  int64_t dest_frame_count = static_cast<int64_t>(dest_samples.size());
  int64_t dest_offset = 0;

  // All source samples should be accumulated into destination samples as-is.
  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kUnity, .scale = kUnityGainScale},
                   /*accumulate=*/true);
  EXPECT_EQ(dest_offset, dest_frame_count);
  EXPECT_EQ(source_offset, Fixed(source_frame_count)) << ffl::String::DecRational << source_offset;
  EXPECT_THAT(dest_samples, Pointwise(FloatEq(), std::vector<float>{1.1f, 0.8f, 1.3f, 0.6f, 1.5f}));
}

TEST(PointSamplerTest, ProcessWithConstantGain) {
  // Create sampler.
  auto sampler = PointSampler::Create(CreateFormat(1, 48000, SampleType::kFloat32),
                                      CreateFormat(1, 48000, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  const std::vector<float> source_samples = {0.1f, -0.2f, 0.3f, -0.4f, 0.5f};
  const int64_t source_frame_count = static_cast<int64_t>(source_samples.size());
  Fixed source_offset = Fixed(0);

  // Start with existing samples to accumulate.
  std::vector<float> dest_samples(5, 1.0f);
  int64_t dest_frame_count = static_cast<int64_t>(dest_samples.size());
  int64_t dest_offset = 0;

  // Source samples should be scaled with constant gain and accumulated into destination samples.
  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kNonUnity, .scale = 10.0f},
                   /*accumulate=*/true);
  EXPECT_EQ(dest_offset, dest_frame_count);
  EXPECT_EQ(source_offset, Fixed(source_frame_count)) << ffl::String::DecRational << source_offset;
  EXPECT_THAT(dest_samples,
              Pointwise(FloatEq(), std::vector<float>{2.0f, -1.0f, 4.0f, -3.0f, 6.0f}));
}

TEST(PointSamplerTest, ProcessWithRampingGain) {
  // Create sampler.
  auto sampler = PointSampler::Create(CreateFormat(1, 48000, SampleType::kFloat32),
                                      CreateFormat(1, 48000, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  const std::vector<float> source_samples = {0.1f, -0.2f, 0.3f, -0.4f, 0.5f};
  const int64_t source_frame_count = static_cast<int64_t>(source_samples.size());
  Fixed source_offset = Fixed(0);

  // Start with existing samples to accumulate.
  std::vector<float> dest_samples(5, 1.0f);
  int64_t dest_frame_count = static_cast<int64_t>(dest_samples.size());
  int64_t dest_offset = 0;

  // Source samples should be scaled with ramping gain and accumulated into destination samples.
  const std::vector<float> scale_ramp = {2.0f, 4.0f, 6.0f, 8.0f, 10.0f};
  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kRamping, .scale_ramp = scale_ramp.data()},
                   /*accumulate=*/true);
  EXPECT_EQ(dest_offset, dest_frame_count);
  EXPECT_EQ(source_offset, Fixed(source_frame_count)) << ffl::String::DecRational << source_offset;
  EXPECT_THAT(dest_samples,
              Pointwise(FloatEq(), std::vector<float>{1.2f, 0.2f, 2.8f, -2.2f, 6.0f}));
}

TEST(PointSamplerTest, ProcessWithSilentGain) {
  // Create sampler.
  auto sampler = PointSampler::Create(CreateFormat(1, 48000, SampleType::kFloat32),
                                      CreateFormat(1, 48000, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  const std::vector<float> source_samples = {0.1f, -0.2f, 0.3f, -0.4f, 0.5f};
  const int64_t source_frame_count = static_cast<int64_t>(source_samples.size());
  Fixed source_offset = Fixed(0);

  // Start with existing samples to accumulate.
  std::vector<float> dest_samples(5, 1.0f);
  int64_t dest_frame_count = static_cast<int64_t>(dest_samples.size());
  int64_t dest_offset = 0;

  // Nothing should be accumulated into destination samples when gain is silent.
  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kSilent, .scale = 0.0f},
                   /*accumulate=*/true);
  EXPECT_EQ(dest_offset, dest_frame_count);
  EXPECT_EQ(source_offset, Fixed(source_frame_count)) << ffl::String::DecRational << source_offset;
  EXPECT_THAT(dest_samples, Each(1.0f));

  // If no accumulation, destination samples should be filled with zeros.
  source_offset = Fixed(0);
  dest_offset = 0;
  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kSilent, .scale = 0.0f},
                   /*accumulate=*/false);
  EXPECT_EQ(dest_offset, dest_frame_count);
  EXPECT_EQ(source_offset, Fixed(source_frame_count)) << ffl::String::DecRational << source_offset;
  EXPECT_THAT(dest_samples, Each(0.0f));
}

TEST(PointSamplerTest, ProcessWithSourceOffsetEqualsDest) {
  // Create sampler.
  auto sampler = PointSampler::Create(CreateFormat(1, 48000, SampleType::kFloat32),
                                      CreateFormat(1, 48000, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  const std::vector<float> source_samples = {0.1f, -0.2f, 0.3f, -0.4f, 0.5f};
  const int64_t source_frame_count = static_cast<int64_t>(source_samples.size());
  Fixed source_offset = Fixed(2);

  // Start with existing samples to accumulate.
  std::vector<float> dest_samples(5, 1.0f);
  int64_t dest_frame_count = 4;
  int64_t dest_offset = 1;

  // Source samples `[2, 3, 4]` should be accumulated into destination samples `[1, 2, 3]`.
  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kUnity, .scale = kUnityGainScale},
                   /*accumulate=*/true);
  EXPECT_EQ(dest_offset, dest_frame_count);
  EXPECT_EQ(source_offset, Fixed(source_frame_count)) << ffl::String::DecRational << source_offset;
  EXPECT_THAT(dest_samples, Pointwise(FloatEq(), std::vector<float>{1.0f, 1.3f, 0.6f, 1.5f, 1.0f}));
}

TEST(PointSamplerTest, ProcessWithSourceOffsetExceedsDest) {
  // Create sampler.
  auto sampler = PointSampler::Create(CreateFormat(1, 48000, SampleType::kFloat32),
                                      CreateFormat(1, 48000, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  const std::vector<float> source_samples = {0.1f, -0.2f, 0.3f, -0.4f, 0.5f};
  const int64_t source_frame_count = static_cast<int64_t>(source_samples.size());
  Fixed source_offset = Fixed(0);

  // Start with existing samples to accumulate.
  std::vector<float> dest_samples(5, 1.0f);
  int64_t dest_frame_count = 3;
  int64_t dest_offset = 1;

  // Source samples `[0, 1]` should be accumulated into destination samples `[1, 2]`.
  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kUnity, .scale = kUnityGainScale},
                   /*accumulate=*/true);
  EXPECT_EQ(dest_offset, dest_frame_count);
  EXPECT_EQ(source_offset, Fixed(2)) << ffl::String::DecRational << source_offset;
  EXPECT_THAT(dest_samples, Pointwise(FloatEq(), std::vector<float>{1.0f, 1.1f, 0.8f, 1.0f, 1.0f}));
}

TEST(PointSamplerTest, ProcessWithDestOffsetExceedsSource) {
  // Create sampler.
  auto sampler = PointSampler::Create(CreateFormat(1, 48000, SampleType::kFloat32),
                                      CreateFormat(1, 48000, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  const std::vector<float> source_samples = {0.1f, -0.2f, 0.3f, -0.4f, 0.5f};
  const int64_t source_frame_count = 4;
  Fixed source_offset = Fixed(3);

  // Start with existing samples to accumulate.
  std::vector<float> dest_samples(5, 1.0f);
  int64_t dest_frame_count = 5;
  int64_t dest_offset = 0;

  // Source sample `[3]` should be accumulated into destination sample `[0]`.
  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kUnity, .scale = kUnityGainScale},
                   /*accumulate=*/true);
  EXPECT_EQ(dest_offset, 1);
  EXPECT_EQ(source_offset, Fixed(source_frame_count)) << ffl::String::DecRational << source_offset;
  EXPECT_THAT(dest_samples, Pointwise(FloatEq(), std::vector<float>{0.6f, 1.0f, 1.0f, 1.0f, 1.0f}));
}

TEST(PointSamplerTest, ProcessWithSourceOffsetAtEnd) {
  // Create sampler.
  auto sampler = PointSampler::Create(CreateFormat(1, 48000, SampleType::kFloat32),
                                      CreateFormat(1, 48000, SampleType::kFloat32));
  ASSERT_THAT(sampler, NotNull());

  const std::vector<float> source_samples = {0.1f, -0.2f, 0.3f, -0.4f, 0.5f};
  const int64_t source_frame_count = static_cast<int64_t>(source_samples.size());
  const Fixed end_offset =
      Fixed(source_frame_count) - sampler->pos_filter_length() + Fixed::FromRaw(1);
  Fixed source_offset = end_offset;

  std::vector<float> dest_samples(4, 0.0f);
  const int64_t dest_frame_count = static_cast<int64_t>(dest_samples.size());
  int64_t dest_offset = 0;

  // Source sample `[3]` should be accumulated into destination sample `[0]`.
  sampler->Process({source_samples.data(), &source_offset, source_frame_count},
                   {dest_samples.data(), &dest_offset, dest_frame_count},
                   {.type = GainType::kUnity, .scale = kUnityGainScale},
                   /*accumulate=*/true);
  EXPECT_EQ(dest_offset, 0);
  EXPECT_EQ(source_offset, end_offset);
  EXPECT_THAT(dest_samples, Each(0.0f));
}

class ProcessWithFractionalSourceOffsetTest : public testing::TestWithParam<Fixed> {
 protected:
  template <typename SourceSampleType>
  void TestPassthrough(uint32_t channel_count, SampleType source_sample_format,
                       const std::vector<SourceSampleType>& source_samples) {
    // Create sampler.
    auto sampler = PointSampler::Create(CreateFormat(channel_count, 48000, source_sample_format),
                                        CreateFormat(channel_count, 48000, SampleType::kFloat32));
    ASSERT_THAT(sampler, NotNull());
    EXPECT_EQ(sampler->pos_filter_length(), Fixed::FromRaw(kFracHalfFrame + 1));
    EXPECT_EQ(sampler->neg_filter_length(), kHalfFrame);

    // Process samples with unity gain.
    const int64_t frame_count = static_cast<int64_t>(source_samples.size() / channel_count);

    Fixed source_offset = GetParam();
    std::vector<float> dest_samples(source_samples.size(), 0.0f);
    int64_t dest_offset = 0;

    sampler->Process({source_samples.data(), &source_offset, frame_count},
                     {dest_samples.data(), &dest_offset, frame_count},
                     {.type = GainType::kUnity, .scale = kUnityGainScale},
                     /*accumulate=*/false);
    EXPECT_EQ(dest_offset, frame_count);
    EXPECT_EQ((source_offset), Fixed(frame_count) + GetParam());
    for (int i = 0; i < frame_count; ++i) {
      EXPECT_FLOAT_EQ(SampleConverter<SourceSampleType>::ToFloat(source_samples[i]),
                      dest_samples[i])
          << i;
    }
  }

  template <uint32_t SourceChannelCount, uint32_t DestChannelCount>
  void TestRechannelization(const std::vector<float>& source_samples,
                            const std::vector<float>& expected_dest_samples) {
    // Create sampler.
    auto sampler =
        PointSampler::Create(CreateFormat(SourceChannelCount, 48000, SampleType::kFloat32),
                             CreateFormat(DestChannelCount, 48000, SampleType::kFloat32));
    EXPECT_EQ(sampler->pos_filter_length(), Fixed::FromRaw(kFracHalfFrame + 1));
    EXPECT_EQ(sampler->neg_filter_length(), kHalfFrame);

    // Process samples with unity gain.
    const int64_t frame_count = static_cast<int64_t>(source_samples.size() / SourceChannelCount);
    ASSERT_EQ(frame_count * DestChannelCount, static_cast<int64_t>(expected_dest_samples.size()));

    Fixed source_offset = GetParam();
    std::vector<float> dest_samples(expected_dest_samples.size(), 0.0f);
    int64_t dest_offset = 0;

    sampler->Process({source_samples.data(), &source_offset, frame_count},
                     {dest_samples.data(), &dest_offset, frame_count},
                     {.type = GainType::kUnity, .scale = kUnityGainScale},
                     /*accumulate=*/false);
    EXPECT_EQ(dest_offset, frame_count);
    EXPECT_EQ((source_offset), Fixed(frame_count) + GetParam());
    EXPECT_THAT(dest_samples, Pointwise(FloatEq(), expected_dest_samples));
  }
};

TEST_P(ProcessWithFractionalSourceOffsetTest, PassthroughUint8) {
  const std::vector<uint8_t> source_samples = {0x00, 0xFF, 0x27, 0xCD, 0x7F, 0x80, 0xA6, 0x6D};

  // Test mono.
  TestPassthrough<uint8_t>(/*channel_count=*/1, SampleType::kUint8, source_samples);

  // Test stereo.
  TestPassthrough<uint8_t>(/*channel_count=*/2, SampleType::kUint8, source_samples);

  // Test 4 channels.
  TestPassthrough<uint8_t>(/*channel_count=*/4, SampleType::kUint8, source_samples);
}

TEST_P(ProcessWithFractionalSourceOffsetTest, PassthroughInt16) {
  const std::vector<int16_t> source_samples = {-0x8000, 0x7FFF, -0x67A7, 0x4D4D,
                                               -0x123,  0,      0x2600,  -0x2DCB};

  // Test mono.
  TestPassthrough<int16_t>(/*channel_count=*/1, SampleType::kInt16, source_samples);

  // Test stereo.
  TestPassthrough<int16_t>(/*channel_count=*/2, SampleType::kInt16, source_samples);

  // Test 4 channels.
  TestPassthrough<int16_t>(/*channel_count=*/4, SampleType::kInt16, source_samples);
}

TEST_P(ProcessWithFractionalSourceOffsetTest, PassthroughInt24In32) {
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

TEST_P(ProcessWithFractionalSourceOffsetTest, PassthroughFloat) {
  const std::vector<float> source_samples = {
      -1.0, 1.0f, -0.809783935f, 0.603912353f, -0.00888061523f, 0.0f, 0.296875f, -0.357757568f};

  // Test mono.
  TestPassthrough<float>(/*channel_count=*/1, SampleType::kFloat32, source_samples);

  // Test stereo.
  TestPassthrough<float>(/*channel_count=*/2, SampleType::kFloat32, source_samples);

  // Test 4 channels.
  TestPassthrough<float>(/*channel_count=*/4, SampleType::kFloat32, source_samples);
}

TEST_P(ProcessWithFractionalSourceOffsetTest, RechannelizationMono) {
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

TEST_P(ProcessWithFractionalSourceOffsetTest, RechannelizationStereo) {
  const std::vector<float> source_samples = {-1.0f, 1.0f, 0.3f, 0.1f};

  // Test stereo to mono.
  TestRechannelization<2, 1>(source_samples, {0.0f, 0.2f});

  // Test stereo to 3 channels.
  TestRechannelization<2, 3>(source_samples, {-1.0f, 1.0f, 0.0f, 0.3f, 0.1f, 0.2f});

  // Test stereo to quad.
  TestRechannelization<2, 4>(source_samples, {-1.0f, 1.0f, -1.0f, 1.0f, 0.3f, 0.1f, 0.3f, 0.1f});
}

TEST_P(ProcessWithFractionalSourceOffsetTest, RechannelizationQuad) {
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

INSTANTIATE_TEST_SUITE_P(PointSamplerTest, ProcessWithFractionalSourceOffsetTest,
                         testing::Values(-kHalfFrame, Fixed(0),
                                         ffl::FromRaw<kPtsFractionalBits>(kFracHalfFrame - 1)));

}  // namespace
}  // namespace media_audio

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/v1/mixer/sinc_sampler.h"

#include <iomanip>
#include <iterator>
#include <memory>

#include <fbl/algorithm.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/format/constants.h"
#include "src/media/audio/lib/processing/filter.h"
#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/lib/timeline/timeline_rate.h"

namespace media::audio::mixer {
namespace {

class SincSamplerTest : public testing::Test {
 protected:
  std::unique_ptr<Mixer> SelectSincSampler(
      int32_t source_channels, int32_t dest_channels, int32_t source_frame_rate,
      int32_t dest_frame_rate, fuchsia::media::AudioSampleFormat source_format,
      fuchsia::media::AudioSampleFormat dest_format = fuchsia::media::AudioSampleFormat::FLOAT) {
    fuchsia::media::AudioStreamType source_stream_type;
    source_stream_type.channels = source_channels;
    source_stream_type.frames_per_second = source_frame_rate;
    source_stream_type.sample_format = source_format;

    fuchsia::media::AudioStreamType dest_stream_type;
    dest_stream_type.channels = dest_channels;
    dest_stream_type.frames_per_second = dest_frame_rate;
    dest_stream_type.sample_format = dest_format;

    return Mixer::Select(source_stream_type, dest_stream_type, Mixer::Resampler::WindowedSinc);
  }

  // These are common frame rates, not the only supported rates
  static constexpr int32_t kFrameRates[] = {
      8000,  11025, 16000, 22050, 24000,  32000,
      44100, 48000, 88200, 96000, 176400, fuchsia::media::MAX_PCM_FRAMES_PER_SECOND,
  };

  static constexpr int32_t kUnsupportedFrameRates[] = {
      fuchsia::media::MIN_PCM_FRAMES_PER_SECOND - 1, fuchsia::media::MAX_PCM_FRAMES_PER_SECOND + 1};

  static constexpr std::pair<int32_t, int32_t> kChannelConfigs[] = {
      {1, 1}, {1, 2}, {1, 3}, {1, 4},  // Valid channel
      {2, 1}, {2, 2}, {2, 3}, {2, 4},  // configurations
      {3, 1}, {3, 2}, {3, 3},          // for SincSampler
      {4, 1}, {4, 2}, {4, 4},
  };

  static constexpr std::pair<int32_t, int32_t> kUnsupportedChannelConfigs[] = {
      {0, 0},                          //
      {1, 0}, {1, 5}, {1, 8}, {1, 9},  // Unsupported channel
      {2, 0}, {2, 5}, {2, 8}, {2, 9},  // channel
      {3, 4}, {3, 5}, {3, 8}, {3, 9},  // configurations --
      {4, 3}, {4, 5}, {4, 7}, {4, 9},  // maximum number of
      {5, 1}, {5, 5},                  // channels is 8.
      {9, 0}, {9, 1}, {9, 9},
  };

  static constexpr fuchsia::media::AudioSampleFormat kFormats[] = {
      fuchsia::media::AudioSampleFormat::UNSIGNED_8,
      fuchsia::media::AudioSampleFormat::SIGNED_16,
      fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32,
      fuchsia::media::AudioSampleFormat::FLOAT,
  };

  static constexpr fuchsia::media::AudioSampleFormat kInvalidFormat =
      static_cast<fuchsia::media::AudioSampleFormat>(
          static_cast<int64_t>(kFormats[std::size(kFormats) - 1]) + 1);
};

// These formats are supported
TEST_F(SincSamplerTest, Construction) {
  // Try every combination of the above
  for (auto channel_config : kChannelConfigs) {
    for (auto source_rate : kFrameRates) {
      for (auto dest_rate : kFrameRates) {
        for (auto format : kFormats) {
          auto mixer = SelectSincSampler(channel_config.first, channel_config.second, source_rate,
                                         dest_rate, format);
          EXPECT_NE(mixer, nullptr);
        }
      }
    }
  }
}

// These formats are unsupported
TEST_F(SincSamplerTest, Construction_UnsupportedRates) {
  for (auto good_rate : kFrameRates) {
    for (auto bad_rate : kUnsupportedFrameRates) {
      // Use channel configs and formats that are known-good.
      auto channel_config = kChannelConfigs[0];
      auto format = kFormats[0];

      SCOPED_TRACE(testing::Message()
                   << "Chans " << channel_config.first << ">" << channel_config.second << ", rates "
                   << good_rate << ":" << bad_rate << ", format " << static_cast<int64_t>(format));
      EXPECT_EQ(nullptr, SelectSincSampler(channel_config.first, channel_config.second, good_rate,
                                           bad_rate, format));

      SCOPED_TRACE(testing::Message()
                   << "Chans " << channel_config.first << ">" << channel_config.second << ", rates "
                   << bad_rate << ":" << good_rate << ", format " << static_cast<int64_t>(format));
      EXPECT_EQ(nullptr, SelectSincSampler(channel_config.first, channel_config.second, bad_rate,
                                           good_rate, format));

      channel_config = kChannelConfigs[std::size(kChannelConfigs) - 1];
      format = kFormats[std::size(kFormats) - 1];

      SCOPED_TRACE(testing::Message()
                   << "Chans " << channel_config.first << ">" << channel_config.second << ", rates "
                   << good_rate << ":" << bad_rate << ", format " << static_cast<int64_t>(format));
      EXPECT_EQ(nullptr, SelectSincSampler(channel_config.first, channel_config.second, good_rate,
                                           bad_rate, format));

      SCOPED_TRACE(testing::Message()
                   << "Chans " << channel_config.first << ">" << channel_config.second << ", rates "
                   << bad_rate << ":" << good_rate << ", format " << static_cast<int64_t>(format));
      EXPECT_EQ(nullptr, SelectSincSampler(channel_config.first, channel_config.second, bad_rate,
                                           good_rate, format));
    }
  }
}

TEST_F(SincSamplerTest, Construction_UnsupportedChannelConfig) {
  for (auto bad_channel_config : kUnsupportedChannelConfigs) {
    // Use rates and formats that are known-good.
    auto source_rate = kFrameRates[0];
    auto dest_rate = source_rate;
    auto format = fuchsia::media::AudioSampleFormat::SIGNED_16;

    SCOPED_TRACE(testing::Message() << "Chans " << bad_channel_config.first << ">"
                                    << bad_channel_config.second << ", rates " << source_rate << ":"
                                    << dest_rate << ", format " << static_cast<int64_t>(format));
    EXPECT_EQ(nullptr, SelectSincSampler(bad_channel_config.first, bad_channel_config.second,
                                         source_rate, dest_rate, format));

    source_rate = kFrameRates[std::size(kFrameRates) - 1];
    dest_rate = source_rate;
    format = fuchsia::media::AudioSampleFormat::FLOAT;

    SCOPED_TRACE(testing::Message() << "Chans " << bad_channel_config.first << ">"
                                    << bad_channel_config.second << ", rates " << source_rate << ":"
                                    << dest_rate << ", format " << static_cast<int64_t>(format));
    EXPECT_EQ(nullptr, SelectSincSampler(bad_channel_config.first, bad_channel_config.second,
                                         source_rate, dest_rate, format));
  }
}

TEST_F(SincSamplerTest, Construction_UnsupportedFormat) {
  // Use channel configs and rates that are known-good.
  auto channel_config = kChannelConfigs[0];
  auto source_rate = kFrameRates[0];
  auto dest_rate = source_rate;

  // bad format: one more than the last enum
  auto bad_format = kInvalidFormat;
  SCOPED_TRACE(testing::Message() << "Chans " << channel_config.first << ">"
                                  << channel_config.second << ", rates " << source_rate << ":"
                                  << dest_rate << ", format " << static_cast<int64_t>(bad_format));
  EXPECT_EQ(nullptr, SelectSincSampler(channel_config.first, channel_config.second, source_rate,
                                       dest_rate, bad_format));
}

class SincSamplerOutputTest : public SincSamplerTest {
 protected:
  // Based on an arbitrary near-zero source position (-1/128), with a sinc curve for unity rate
  // conversion, we use data values calculated so that if these first 13 values (the filter's
  // negative wing) are ignored, we expect a generated output value of kValueWithoutPreviousFrames.
  // If they are NOT ignored, then we expect the result kValueWithPreviousFrames.
  static constexpr float kSource[] = {
      1330.10897f, -1330.10897f, 1330.10897f, -1330.10897f, 1330.10897f, -1330.10897f,
      1330.10897f, -1330.10897f, 1330.10897f, -1330.10897f, 1330.10897f, -1330.10897f,
      1330.10897f,  // ... source frames to satisfy negative filter width.
      -10.001010f,  // Center source frame
      268.88298f,   // Source frames to satisfy positive filter width ...
      -268.88298f, 268.88298f,   -268.88298f, 268.88298f,   -268.88298f, 268.88298f,
      -268.88298f, 268.88298f,   -268.88298f, 268.88298f,   -268.88298f, 268.88298f,
  };
  static constexpr Fixed kMixOneFrameSourceOffset = ffl::FromRatio(1, 128);
  // The center frame should contribute -10.0, the positive wing -5.0, and the negative wing +25.0.
  static constexpr float kValueWithoutPreviousFrames = -15.0;
  static constexpr float kValueWithPreviousFrames = 10.0;

  float MixOneFrame(std::unique_ptr<Mixer>& mixer, Fixed source_offset);
};

// Validate the "seam" between buffers, at unity rate-conversion
TEST_F(SincSamplerOutputTest, UnityConstant) {
  constexpr int32_t kSourceRate = 44100;
  constexpr int32_t kDestRate = 44100;
  auto mixer =
      SelectSincSampler(1, 1, kSourceRate, kDestRate, fuchsia::media::AudioSampleFormat::FLOAT);
  ASSERT_NE(mixer, nullptr);

  bool do_not_accum = false;

  constexpr int32_t kDestLen = 512;
  int64_t dest_offset = 0;
  auto dest = std::make_unique<float[]>(kDestLen);

  constexpr int32_t kSourceLen = kDestLen / 2;
  auto source_offset = Fixed(0);
  auto source = std::make_unique<float[]>(kSourceLen);
  for (auto idx = 0u; idx < kSourceLen; ++idx) {
    source[idx] = 1.0f;
  }

  mixer->state().ResetSourceStride(TimelineRate(Fixed(1).raw_value(), 1));

  // Mix the first half of the destination
  mixer->Mix(dest.get(), kDestLen, &dest_offset, source.get(), kSourceLen, &source_offset,
             do_not_accum);
  EXPECT_GE(source_offset + mixer->pos_filter_width(), Fixed(kSourceLen));
  EXPECT_EQ(source_offset.Floor(), dest_offset);
  auto first_half_dest = dest_offset;

  // Now mix the rest
  source_offset -= Fixed(kSourceLen);
  mixer->Mix(dest.get(), kDestLen, &dest_offset, source.get(), kSourceLen, &source_offset,
             do_not_accum);
  EXPECT_GE(source_offset + mixer->pos_filter_width(), Fixed(kSourceLen));

  // The "seam" between buffers should be invisible
  for (auto idx = first_half_dest - 2; idx < first_half_dest + 2; ++idx) {
    EXPECT_NEAR(dest[idx], 1.0f, 0.001f);
  }
}

// Validate the "seam" between buffers, while down-sampling
TEST_F(SincSamplerOutputTest, DownSampleConstant) {
  constexpr int32_t kSourceRate = 48000;
  constexpr int32_t kDestRate = 44100;
  auto mixer =
      SelectSincSampler(1, 1, kSourceRate, kDestRate, fuchsia::media::AudioSampleFormat::FLOAT);
  ASSERT_NE(mixer, nullptr);

  bool do_not_accum = false;

  constexpr int32_t kDestLen = 512;
  int64_t dest_offset = 0;
  auto dest = std::make_unique<float[]>(kDestLen);

  constexpr int32_t kSourceLen = kDestLen / 2;
  auto source_offset = Fixed(0);
  auto source = std::make_unique<float[]>(kSourceLen);
  for (auto idx = 0u; idx < kSourceLen; ++idx) {
    source[idx] = 1.0f;
  }

  mixer->state().ResetSourceStride(TimelineRate(Fixed(kSourceRate).raw_value(), kDestRate));

  // Mix the first half of the destination
  mixer->Mix(dest.get(), kDestLen, &dest_offset, source.get(), kSourceLen, &source_offset,
             do_not_accum);
  EXPECT_GE(source_offset + mixer->pos_filter_width(), Fixed(kSourceLen));
  auto first_half_dest = dest_offset;

  // Now mix the rest
  source_offset -= Fixed(kSourceLen);
  mixer->Mix(dest.get(), kDestLen, &dest_offset, source.get(), kSourceLen, &source_offset,
             do_not_accum);
  EXPECT_GE(source_offset + mixer->pos_filter_width(), Fixed(kSourceLen));

  // The "seam" between buffers should be invisible
  for (auto idx = first_half_dest - 2; idx < first_half_dest + 2; ++idx) {
    EXPECT_NEAR(dest[idx], 1.0f, 0.001f);
  }
}

// Validate the "seam" between buffers, while up-sampling
TEST_F(SincSamplerOutputTest, UpSampleConstant) {
  constexpr int32_t kSourceRate = 12000;
  constexpr int32_t kDestRate = 48000;
  auto mixer =
      SelectSincSampler(1, 1, kSourceRate, kDestRate, fuchsia::media::AudioSampleFormat::FLOAT);
  ASSERT_NE(mixer, nullptr);

  bool do_not_accum = false;

  constexpr int32_t kDestLen = 1024;
  int64_t dest_offset = 0;
  auto dest = std::make_unique<float[]>(kDestLen);

  constexpr int32_t kSourceLen = kDestLen / 8;
  auto source_offset = Fixed(0);
  auto source = std::make_unique<float[]>(kSourceLen);
  for (auto idx = 0u; idx < kSourceLen; ++idx) {
    source[idx] = 1.0f;
  }

  mixer->state().ResetSourceStride(TimelineRate(Fixed(kSourceRate).raw_value(), kDestRate));

  // Mix the first half of the destination
  mixer->Mix(dest.get(), kDestLen / 2, &dest_offset, source.get(), kSourceLen, &source_offset,
             do_not_accum);
  EXPECT_GE(source_offset + mixer->pos_filter_width(), Fixed(kSourceLen));
  EXPECT_EQ(Fixed(source_offset * 4).Floor(), dest_offset);
  auto first_half_dest = dest_offset;

  // Now mix the rest
  source_offset -= Fixed(kSourceLen);
  mixer->Mix(dest.get(), kDestLen, &dest_offset, source.get(), kSourceLen, &source_offset,
             do_not_accum);
  EXPECT_GE(source_offset + mixer->pos_filter_width(), Fixed(kSourceLen));

  // The two samples before and after the "seam" between buffers should be invisible
  for (auto idx = first_half_dest - 2; idx < first_half_dest + 2; ++idx) {
    EXPECT_NEAR(dest[idx], 1.0f, 0.001f);
  }
}

// Mix a single frame of output based on kSource[0]. Producing a frame for position 0 requires
// neg_width previous frames, kSource[0] itself, and pos_width frames beyond kSource[0].
// Used by tests that do simple mixing and need not inspect the returned position values.
float SincSamplerOutputTest::MixOneFrame(std::unique_ptr<Mixer>& mixer, Fixed source_offset) {
  auto neg_width = mixer->neg_filter_width().Floor();
  auto pos_width = mixer->pos_filter_width().Floor();
  EXPECT_NE(Fixed(pos_width).raw_value() + 1, mixer->neg_filter_width().raw_value())
      << "This test assumes SincSampler is symmetric, and that negative width includes a "
         "fraction";

  float dest;
  int64_t dest_offset = 0;
  int64_t source_frames = pos_width + 1;

  mixer->Mix(&dest, 1, &dest_offset, &(kSource[neg_width]), source_frames, &source_offset, false);
  EXPECT_EQ(dest_offset, 1u) << "No output frame was produced";

  FX_LOGS(INFO) << "Coefficients " << std::setprecision(12) << kSource[12] << " " << kSource[13]
                << " " << kSource[14] << ", value " << dest;

  return dest;
}

// Mix a single frame, without any previously-cached data.
TEST_F(SincSamplerOutputTest, MixOneNoCache) {
  auto mixer = SelectSincSampler(1, 1, 44100, 44100, fuchsia::media::AudioSampleFormat::FLOAT);
  ASSERT_NE(mixer, nullptr);

  // Mix a single frame. We use a slightly non-zero position because at true 0, only the sample
  // (not the positive or negative wings) are used. In this case we not provided previous frames.
  float dest = MixOneFrame(mixer, -kMixOneFrameSourceOffset);

  // If we incorrectly shifted/retained even a single frame of the above data, this won't match.
  EXPECT_FLOAT_EQ(dest, kValueWithoutPreviousFrames) << std::setprecision(12) << dest;
}

// Mix a single frame, with previously-cached data.
TEST_F(SincSamplerOutputTest, MixOneWithCache) {
  auto mixer = SelectSincSampler(1, 1, 44100, 44100, fuchsia::media::AudioSampleFormat::FLOAT);
  ASSERT_NE(mixer, nullptr);
  auto neg_width = mixer->neg_filter_width().Floor();

  // Now, populate the cache with previous frames, instead of using default (silence) values.
  // The outparam value of source_offset tells us the cache is populated with neg_width frames,
  // which is ideal for mixing a subsequent source buffer starting at source position [0].
  float dest;
  int64_t dest_offset = 0;
  auto source_frames = neg_width;
  Fixed source_offset = Fixed(source_frames) - kMixOneFrameSourceOffset;

  mixer->Mix(&dest, 1, &dest_offset, &(kSource[0]), source_frames, &source_offset, false);
  EXPECT_EQ(source_offset, Fixed(source_frames) - kMixOneFrameSourceOffset);
  EXPECT_EQ(dest_offset, 0u) << "Unexpectedly produced output " << dest;

  // Mix a single frame. We use a slightly non-zero position because at true 0, only the sample
  // itself (not positive or negative widths) are needed. In this case we provide previous frames.
  dest = MixOneFrame(mixer, -kMixOneFrameSourceOffset);

  // If we incorrectly shifted/retained even a single frame of the above data, this won't match.
  EXPECT_FLOAT_EQ(dest, kValueWithPreviousFrames) << std::setprecision(12) << dest;
}

// Mix a single frame, after feeding the cache with previous data, one frame at a time.
// Specifying source_offset >= 0 guarantees that the cached source data will be shifted
// appropriately, so that subsequent Mix() calls can correctly use that data.
TEST_F(SincSamplerOutputTest, MixFrameByFrameCached) {
  auto mixer = SelectSincSampler(1, 1, 44100, 44100, fuchsia::media::AudioSampleFormat::FLOAT);
  ASSERT_NE(mixer, nullptr);
  auto neg_width = mixer->neg_filter_width().Floor();

  // Now, populate the cache with previous data, one frame at a time.
  float dest;
  int64_t dest_offset = 0;
  const auto source_frames = 1;
  Fixed source_offset = Fixed(source_frames) - kMixOneFrameSourceOffset;

  for (auto neg_idx = 0u; neg_idx < neg_width; ++neg_idx) {
    mixer->Mix(&dest, 1, &dest_offset, &(kSource[neg_idx]), source_frames, &source_offset, false);
    EXPECT_EQ(source_offset, Fixed(source_frames) - kMixOneFrameSourceOffset);
    EXPECT_EQ(dest_offset, 0u) << "Unexpectedly produced output " << dest;
  }

  // Mix a single frame. We use a slightly non-zero position because at true 0, only the sample
  // itself (not positive or negative widths) are needed. In this case we provide previous frames.
  dest = MixOneFrame(mixer, -kMixOneFrameSourceOffset);

  // If we incorrectly shifted/retained even a single frame of the above data, this won't match.
  EXPECT_FLOAT_EQ(dest, kValueWithPreviousFrames) << std::setprecision(12) << dest;
}

// Tests of the SincSampler's advancing of source and dest position. These tests do not use
// meaningful source data values, nor check the values of the data returned from Mix. Only the
// change in source_offset and dest_offset (and the Mix() return value) are evaluated.
class SincSamplerPositionTest : public SincSamplerTest {
 protected:
  void TestFractionalPositionAtFrameBoundary(bool mute);
  void TestFractionalPositionJustBeforeFrameBoundary(bool mute);
  void TestSourceOffsetAtEnd(bool mute);
  void TestRateModulo(bool mute);
  void TestPositionModuloFromZeroNoRollover(bool mute);
  void TestPositionModuloFromNonZeroNoRollover(bool mute);
  void TestPositionModuloFromZeroRollover(bool mute);
  void TestPositionModuloFromNonZeroRollover(bool mute);
  void TestSourcePosModuloExactRolloverForCompletion(bool mute);
};

TEST_F(SincSamplerPositionTest, FilterWidth) {
  auto mixer = SelectSincSampler(1, 1, 48000, 48000, fuchsia::media::AudioSampleFormat::FLOAT);
  ASSERT_NE(mixer, nullptr);

  EXPECT_EQ(mixer->pos_filter_width().raw_value(), media_audio::SincFilter::kFracSideLength - 1);
  EXPECT_EQ(mixer->neg_filter_width().raw_value(), media_audio::SincFilter::kFracSideLength - 1);
}

// Test basic position advancing, for integer rate and same-sized source and dest buffers.
TEST_F(SincSamplerPositionTest, SameFrameRate) {
  auto mixer = SelectSincSampler(1, 1, 48000, 48000, fuchsia::media::AudioSampleFormat::FLOAT);
  ASSERT_NE(mixer, nullptr);

  std::array<float, 50> source{0.0f};
  const int64_t source_frames = 20;
  Fixed source_offset = ffl::FromRatio(3, 4);

  std::array<float, 50> accum{0.0f};
  const int64_t dest_frames = accum.size();
  int64_t dest_offset = 0;

  int64_t expect_advance =
      Fixed(Fixed(source_frames) - mixer->pos_filter_width() - source_offset).Ceiling();
  Fixed expect_source_offset = source_offset + Fixed(expect_advance);
  int64_t expect_dest_offset = dest_offset + expect_advance;

  // Pass in 20 frames
  mixer->Mix(accum.data(), dest_frames, &dest_offset, source.data(), source_frames, &source_offset,
             false);
  EXPECT_EQ(dest_offset, expect_dest_offset);
  EXPECT_EQ(source_offset, expect_source_offset);
}

// When talking about amounts of supply and demand ("has" and "wants"), we automatically include
// pos_filter_width for clarity, rather than explicitly mentioning this each time. Thus if setting
// source_offset to "Fixed(source_frames - 4) - mixer->pos_filter_width()", we consider this exactly
// 4 frames before the end of the source buffer, so we say "Source (offset 46.00 of 50) has 4."
// Also, for purposes of comparing supply and demand, fractional source amounts can be rounded up:
// something like "Source (offset 0.3 of 3) has 2.7(3)" means we can sample at 0.3, 1.3 and 2.3.

// For SincSampler, test sample placement when given fractional position offsets. We test on both
// sides of the boundary between "do we have enough source data to produce the next frame?"
// These tests use fractional offsets, still with a step_size of ONE.
//
// Check: factoring in positive filter width, source position is exactly at a frame boundary.
//
// Position accounting uses different code when muted, so also run these position tests when muted.
void SincSamplerPositionTest::TestFractionalPositionAtFrameBoundary(bool mute) {
  auto mixer = SelectSincSampler(1, 1, 44100, 44100, fuchsia::media::AudioSampleFormat::SIGNED_16);
  ASSERT_NE(mixer, nullptr);

  // Source (offset 46.00 of 50) has 4. Dest (offset 1 of 10) wants 9. Expect to advance by 4.
  std::array<float, 50> source{0.0f};
  const int64_t source_frames = source.size();
  Fixed source_offset = Fixed(source_frames - 4) - mixer->pos_filter_width();

  std::array<float, 10> accum{0.0f};
  const int64_t dest_frames = accum.size();
  int64_t dest_offset = 1;

  int64_t expect_advance = 4;
  Fixed expect_source_offset = source_offset + Fixed(expect_advance);
  int64_t expect_dest_offset = dest_offset + expect_advance;

  mixer->gain.SetSourceGain(mute ? media_audio::kMinGainDb : media_audio::kUnityGainDb);
  mixer->Mix(accum.data(), dest_frames, &dest_offset, source.data(), source_frames, &source_offset,
             true);

  EXPECT_EQ(dest_offset, expect_dest_offset);
  EXPECT_EQ(source_offset, expect_source_offset) << ffl::String::DecRational << source_offset;
}
TEST_F(SincSamplerPositionTest, FractionalPositionAtFrameBoundary) {
  TestFractionalPositionAtFrameBoundary(false);
}
TEST_F(SincSamplerPositionTest, FractionalPositionAtFrameBoundaryMute) {
  TestFractionalPositionAtFrameBoundary(true);
}

// Check: factoring in positive filter width, source position is just short of a frame boundary.
// Thus we should consume an additional frame, compared to the previous testcase.
//
// Position accounting uses different code when muted, so also run these position tests when muted.
void SincSamplerPositionTest::TestFractionalPositionJustBeforeFrameBoundary(bool mute) {
  auto mixer = SelectSincSampler(1, 1, 44100, 44100, fuchsia::media::AudioSampleFormat::SIGNED_16);
  ASSERT_NE(mixer, nullptr);

  // Source (offset 45.99 of 50) has 4.01(5). Dest (offset 1 of 10) wants 9. Expect to advance by 5.
  std::array<float, 50> source{0.0f};
  const int64_t source_frames = source.size();
  Fixed source_offset = Fixed(source_frames - 4) - mixer->pos_filter_width() - Fixed::FromRaw(1);

  std::array<float, 10> accum{0.0f};
  const int64_t dest_frames = accum.size();
  int64_t dest_offset = 1;

  int64_t expect_advance = 5;
  Fixed expect_source_offset = source_offset + Fixed(expect_advance);
  int64_t expect_dest_offset = dest_offset + expect_advance;

  mixer->gain.SetSourceMute(mute);
  mixer->Mix(accum.data(), dest_frames, &dest_offset, source.data(), source_frames, &source_offset,
             true);

  EXPECT_EQ(dest_offset, expect_dest_offset);
  EXPECT_EQ(source_offset, expect_source_offset) << ffl::String::DecRational << source_offset;
}
TEST_F(SincSamplerPositionTest, FractionalPositionJustBeforeFrameBoundary) {
  TestFractionalPositionJustBeforeFrameBoundary(false);
}
TEST_F(SincSamplerPositionTest, FractionalPositionJustBeforeFrameBoundaryMute) {
  TestFractionalPositionJustBeforeFrameBoundary(true);
}

// When frac_source_pos is at the end (or within pos_filter_width) of the source buffer, the sampler
// should not mix additional frames (neither dest_offset nor source_offset should be advanced).
//
// Position accounting uses different code when muted, so also run these position tests when muted.
void SincSamplerPositionTest::TestSourceOffsetAtEnd(bool mute) {
  auto mixer = SelectSincSampler(1, 1, 44100, 44100, fuchsia::media::AudioSampleFormat::FLOAT);
  ASSERT_NE(mixer, nullptr);

  std::array<float, 50> source{0.0f};
  const int64_t source_frames = source.size();
  Fixed source_offset = Fixed(source.size()) - mixer->pos_filter_width();
  const auto initial_source_offset = source_offset;

  std::array<float, 50> accum{0.0f};
  const int64_t dest_frames = accum.size();
  int64_t dest_offset = 0;

  mixer->state().ResetSourceStride(TimelineRate(Fixed(1).raw_value(), 1));
  mixer->gain.SetSourceGain(mute ? media_audio::kMinGainDb : media_audio::kUnityGainDb);
  mixer->Mix(accum.data(), dest_frames, &dest_offset, source.data(), source_frames, &source_offset,
             false);
  EXPECT_EQ(dest_offset, 0);
  EXPECT_EQ(source_offset, initial_source_offset);
  EXPECT_EQ(accum[0], 0.0f);
}
TEST_F(SincSamplerPositionTest, SourceOffsetAtEnd) { TestSourceOffsetAtEnd(false); }
TEST_F(SincSamplerPositionTest, SourceOffsetAtEndMute) { TestSourceOffsetAtEnd(true); }

// Validate that RateModulo is taken into account, in position calculations.
//
// Position accounting uses different code when muted, so also run these position tests when muted.
void SincSamplerPositionTest::TestRateModulo(bool mute) {
  auto mixer = SelectSincSampler(1, 1, 32000, 48000, fuchsia::media::AudioSampleFormat::SIGNED_16);
  ASSERT_NE(mixer, nullptr);

  // Provide the entire large source buffer, so that Mix will be limited by the dest amount.
  std::array<float, 50> source{0.0f};
  const int64_t source_frames = source.size();
  auto source_offset = Fixed(0);
  auto expect_source_offset = Fixed(2);

  std::array<float, 3> accum{0.0f};
  const int64_t dest_frames = accum.size();
  int64_t dest_offset = 0;

  auto& state = mixer->state();
  state.ResetSourceStride(TimelineRate(Fixed(2).raw_value(), 3));
  ASSERT_EQ(state.step_size_modulo(),
            static_cast<uint64_t>(Fixed(Fixed(2) - (state.step_size() * 3)).raw_value()));
  mixer->gain.SetSourceMute(mute);
  mixer->Mix(accum.data(), dest_frames, &dest_offset, source.data(), source_frames, &source_offset,
             false);

  EXPECT_EQ(dest_offset, dest_frames);
  EXPECT_EQ(source_offset, expect_source_offset);
}
TEST_F(SincSamplerPositionTest, RateModulo) { TestRateModulo(false); }
TEST_F(SincSamplerPositionTest, RateModuloMute) { TestRateModulo(true); }

// For "almost-but-not-rollover" cases, we generate 3 output samples, leaving source and dest at pos
// 3 and source_pos_modulo at 9999/10000.

// Case: source_pos_modulo starts at zero, extending to almost-but-not-quite-rollover.
//
// Position accounting uses different code when muted, so also run these position tests when muted.
void SincSamplerPositionTest::TestPositionModuloFromZeroNoRollover(bool mute) {
  auto mixer = SelectSincSampler(1, 1, 44100, 44100, fuchsia::media::AudioSampleFormat::FLOAT);
  ASSERT_NE(mixer, nullptr);

  std::array<float, 50> source{0.0f};
  const int64_t source_frames = source.size();
  Fixed source_offset = Fixed(0);

  std::array<float, 3> accum{0.0f};
  const int64_t dest_frames = accum.size();
  int64_t dest_offset = 0;

  auto& state = mixer->state();
  state.ResetSourceStride(TimelineRate(Fixed(10000).raw_value() + 3333, 10000));
  EXPECT_EQ(state.step_size(), kOneFrame);
  EXPECT_EQ(state.step_size_modulo(), 3333ul);
  EXPECT_EQ(state.step_size_denominator(), 10000ul);

  mixer->gain.SetSourceGain(mute ? media_audio::kMinGainDb : media_audio::kUnityGainDb);
  mixer->Mix(accum.data(), dest_frames, &dest_offset, source.data(), source_frames, &source_offset,
             false);
  EXPECT_EQ(dest_offset, dest_frames);
  EXPECT_EQ(source_offset, Fixed(3)) << ffl::String::DecRational << source_offset;
  EXPECT_EQ(state.source_pos_modulo(), 9999u);
}
TEST_F(SincSamplerPositionTest, SourcePosModuloFromZeroAlmostRollover) {
  TestPositionModuloFromZeroNoRollover(false);
}
TEST_F(SincSamplerPositionTest, SourcePosModuloFromZeroAlmostRolloverMute) {
  TestPositionModuloFromZeroNoRollover(true);
}

// Same as above (ending at two less than rollover), starting source_pos_modulo at a non-zero value.
void SincSamplerPositionTest::TestPositionModuloFromNonZeroNoRollover(bool mute) {
  auto mixer = SelectSincSampler(1, 1, 44100, 44100, fuchsia::media::AudioSampleFormat::FLOAT);
  ASSERT_NE(mixer, nullptr);

  std::array<float, 50> source{0.0f};
  const int64_t source_frames = source.size();  // mix amount is constrained by dest availability
  Fixed source_offset = Fixed(0);

  std::array<float, 3> accum{0.0f};
  const int64_t dest_frames = accum.size();
  int64_t dest_offset = 0;

  auto& state = mixer->state();
  state.ResetSourceStride(TimelineRate(Fixed(10000).raw_value() + 3331, 10000));
  EXPECT_EQ(state.step_size(), kOneFrame);
  EXPECT_EQ(state.step_size_modulo(), 3331ul);
  EXPECT_EQ(state.step_size_denominator(), 10000ul);
  state.set_source_pos_modulo(6);

  mixer->gain.SetSourceMute(mute);
  mixer->Mix(accum.data(), dest_frames, &dest_offset, source.data(), source_frames, &source_offset,
             false);
  EXPECT_EQ(dest_offset, dest_frames);
  EXPECT_EQ(source_offset, Fixed(3)) << ffl::String::DecRational << source_offset;
  EXPECT_EQ(state.source_pos_modulo(), 9999u);
}
TEST_F(SincSamplerPositionTest, SourcePosModuloFromNonZeroAlmostRollover) {
  TestPositionModuloFromNonZeroNoRollover(false);
}
TEST_F(SincSamplerPositionTest, SourcePosModuloFromNonZeroAlmostRolloverMute) {
  TestPositionModuloFromNonZeroNoRollover(true);
}

// These "exact-rollover" cases generate 2 frames, ending at source pos 3, source_pos_mod 0/10000.
//
// Position accounting uses different code when muted, so also run these position tests when muted.
void SincSamplerPositionTest::TestPositionModuloFromZeroRollover(bool mute) {
  auto mixer = SelectSincSampler(1, 1, 44100, 44100, fuchsia::media::AudioSampleFormat::FLOAT);
  ASSERT_NE(mixer, nullptr);

  std::array<float, 50> source{0.0f};
  const int64_t source_frames = source.size();  // mix amount is constrained by dest availability
  Fixed source_offset = Fixed(1) - Fixed::FromRaw(1);

  std::array<float, 3> accum{0.0f};
  const int64_t dest_frames = accum.size();
  int64_t dest_offset = 1;

  auto& state = mixer->state();
  state.ResetSourceStride(TimelineRate(Fixed(10000).raw_value() + 5000, 10000));
  EXPECT_EQ(state.step_size(), kOneFrame);
  EXPECT_EQ(state.step_size_modulo(), 1ul);
  EXPECT_EQ(state.step_size_denominator(), 2ul);

  mixer->gain.SetSourceGain(mute ? media_audio::kMinGainDb : media_audio::kUnityGainDb);
  mixer->Mix(accum.data(), dest_frames, &dest_offset, source.data(), source_frames, &source_offset,
             false);
  EXPECT_EQ(dest_offset, static_cast<int64_t>(dest_frames));
  EXPECT_EQ(source_offset, Fixed(3)) << ffl::String::DecRational << source_offset;
  EXPECT_EQ(state.source_pos_modulo(), 0u);
}
TEST_F(SincSamplerPositionTest, SourcePosModuloFromZeroExactRollover) {
  TestPositionModuloFromZeroRollover(false);
}
TEST_F(SincSamplerPositionTest, SourcePosModuloFromZeroExactRolloverMute) {
  TestPositionModuloFromZeroRollover(true);
}

// Same as above (ending at exactly the rollover point), starting source_pos_modulo at non-zero.
void SincSamplerPositionTest::TestPositionModuloFromNonZeroRollover(bool mute) {
  auto mixer = SelectSincSampler(1, 1, 44100, 44100, fuchsia::media::AudioSampleFormat::FLOAT);
  ASSERT_NE(mixer, nullptr);

  std::array<float, 50> source{0.0f};
  const int64_t source_frames = source.size();  // mix amount is constrained by dest availability
  Fixed source_offset = Fixed(1) - Fixed::FromRaw(1);

  std::array<float, 3> accum{0.0f};
  const int64_t dest_frames = accum.size();
  int64_t dest_offset = 1;

  auto& state = mixer->state();
  state.ResetSourceStride(TimelineRate(Fixed(10000).raw_value() + 3331, 10000));
  EXPECT_EQ(state.step_size(), kOneFrame);
  EXPECT_EQ(state.step_size_modulo(), 3331ul);
  EXPECT_EQ(state.step_size_denominator(), 10000ul);
  state.set_source_pos_modulo(3338);

  mixer->gain.SetSourceMute(mute);
  mixer->Mix(accum.data(), dest_frames, &dest_offset, source.data(), source_frames, &source_offset,
             false);
  EXPECT_EQ(dest_offset, dest_frames);
  EXPECT_EQ(source_offset, Fixed(3)) << ffl::String::DecRational << source_offset;
  EXPECT_EQ(state.source_pos_modulo(), 0u);
}
TEST_F(SincSamplerPositionTest, SourcePosModuloFromNonZeroExactRollover) {
  TestPositionModuloFromNonZeroRollover(false);
}
TEST_F(SincSamplerPositionTest, SourcePosModuloFromNonZeroExactRolloverMute) {
  TestPositionModuloFromNonZeroRollover(true);
}

// For SincSampler, validate a source_pos_modulo rollover precisely before the source buffer's end.
// Example: source_offset starts at 8.000+2/3 (of 10), with rate 0.999+2/3. After two dest frames,
// source_offset is 9.998+6/3 == exactly 10, so we cannot consume an additional frame.
//
// Position accounting uses different code when muted, so also run these position tests when muted.
void SincSamplerPositionTest::TestSourcePosModuloExactRolloverForCompletion(bool mute) {
  auto mixer = SelectSincSampler(1, 1, 44100, 44100, fuchsia::media::AudioSampleFormat::FLOAT);
  ASSERT_NE(mixer, nullptr);

  std::array<float, 10> source{0.0f};
  const int64_t source_frames = source.size();
  Fixed source_offset = Fixed(source_frames) - Fixed(2) - mixer->pos_filter_width();

  std::array<float, 3> accum{0.0f};
  const int64_t dest_frames = accum.size();
  int64_t dest_offset = 0;

  auto& state = mixer->state();
  state.ResetSourceStride(TimelineRate(Fixed(3).raw_value() - 1, 3));
  EXPECT_EQ(state.step_size(), kOneFrame - Fixed::FromRaw(1));
  EXPECT_EQ(state.step_size_modulo(), 2ul);
  EXPECT_EQ(state.step_size_denominator(), 3ul);
  state.set_source_pos_modulo(2);

  mixer->gain.SetSourceGain(mute ? media_audio::kMinGainDb : media_audio::kUnityGainDb);
  mixer->Mix(accum.data(), dest_frames, &dest_offset, source.data(), source_frames, &source_offset,
             false);
  EXPECT_EQ(dest_offset, 2);
  EXPECT_EQ(source_offset, Fixed(Fixed(source_frames) - mixer->pos_filter_width()))
      << ffl::String::DecRational << source_offset;
  EXPECT_EQ(state.source_pos_modulo(), 0u);
}
TEST_F(SincSamplerPositionTest, SourcePosModuloExactRolloverCausesEarlyComplete) {
  TestSourcePosModuloExactRolloverForCompletion(false);
}
TEST_F(SincSamplerPositionTest, SourcePosModuloExactRolloverCausesEarlyCompleteMute) {
  TestSourcePosModuloExactRolloverForCompletion(true);
}

}  // namespace
}  // namespace media::audio::mixer

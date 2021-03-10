// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/sinc_sampler.h"

#include <iterator>
#include <memory>

#include <fbl/algorithm.h>
#include <gtest/gtest.h>

#include "src/media/audio/audio_core/mixer/filter.h"
#include "src/media/audio/lib/format/constants.h"

namespace media::audio::mixer {
namespace {

std::unique_ptr<Mixer> SelectSincSampler(
    uint32_t source_channels, uint32_t dest_channels, uint32_t source_frame_rate,
    uint32_t dest_frame_rate, fuchsia::media::AudioSampleFormat source_format,
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
const uint32_t kFrameRates[] = {
    8000,  11025, 16000, 22050, 24000,  32000,
    44100, 48000, 88200, 96000, 176400, fuchsia::media::MAX_PCM_FRAMES_PER_SECOND,
};

const uint32_t kUnsupportedFrameRates[] = {fuchsia::media::MIN_PCM_FRAMES_PER_SECOND - 1,
                                           fuchsia::media::MAX_PCM_FRAMES_PER_SECOND + 1};

const std::pair<uint32_t, uint32_t> kChannelConfigs[] = {
    {1, 1}, {1, 2}, {1, 3}, {1, 4},  // Valid channel
    {2, 1}, {2, 2}, {2, 3}, {2, 4},  // configurations
    {3, 1}, {3, 2}, {3, 3},          // for SincSampler
    {4, 1}, {4, 2}, {4, 4},
};

const std::pair<uint32_t, uint32_t> kUnsupportedChannelConfigs[] = {
    {0, 0},                          //
    {1, 0}, {1, 5}, {1, 8}, {1, 9},  // Unsupported channel
    {2, 0}, {2, 5}, {2, 8}, {2, 9},  // channel
    {3, 4}, {3, 5}, {3, 8}, {3, 9},  // configurations --
    {4, 3}, {4, 5}, {4, 7}, {4, 9},  // maximum number of
    {5, 1}, {5, 5},                  // channels is 8.
    {9, 0}, {9, 1}, {9, 9},
};

const fuchsia::media::AudioSampleFormat kFormats[] = {
    fuchsia::media::AudioSampleFormat::UNSIGNED_8,
    fuchsia::media::AudioSampleFormat::SIGNED_16,
    fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32,
    fuchsia::media::AudioSampleFormat::FLOAT,
};

const fuchsia::media::AudioSampleFormat kInvalidFormat =
    static_cast<fuchsia::media::AudioSampleFormat>(
        static_cast<uint64_t>(kFormats[std::size(kFormats) - 1]) + 1);

// These formats are supported
TEST(SincSamplerTest, Construction) {
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
TEST(SincSamplerTest, Construction_UnsupportedRates) {
  for (auto good_rate : kFrameRates) {
    for (auto bad_rate : kUnsupportedFrameRates) {
      // Use channel configs and formats that are known-good.
      auto channel_config = kChannelConfigs[0];
      auto format = kFormats[0];

      SCOPED_TRACE(testing::Message()
                   << "Chans " << channel_config.first << ">" << channel_config.second << ", rates "
                   << good_rate << ":" << bad_rate << ", format " << static_cast<uint64_t>(format));
      EXPECT_EQ(nullptr, SelectSincSampler(channel_config.first, channel_config.second, good_rate,
                                           bad_rate, format));

      SCOPED_TRACE(testing::Message()
                   << "Chans " << channel_config.first << ">" << channel_config.second << ", rates "
                   << bad_rate << ":" << good_rate << ", format " << static_cast<uint64_t>(format));
      EXPECT_EQ(nullptr, SelectSincSampler(channel_config.first, channel_config.second, bad_rate,
                                           good_rate, format));

      channel_config = kChannelConfigs[std::size(kChannelConfigs) - 1];
      format = kFormats[std::size(kFormats) - 1];

      SCOPED_TRACE(testing::Message()
                   << "Chans " << channel_config.first << ">" << channel_config.second << ", rates "
                   << good_rate << ":" << bad_rate << ", format " << static_cast<uint64_t>(format));
      EXPECT_EQ(nullptr, SelectSincSampler(channel_config.first, channel_config.second, good_rate,
                                           bad_rate, format));

      SCOPED_TRACE(testing::Message()
                   << "Chans " << channel_config.first << ">" << channel_config.second << ", rates "
                   << bad_rate << ":" << good_rate << ", format " << static_cast<uint64_t>(format));
      EXPECT_EQ(nullptr, SelectSincSampler(channel_config.first, channel_config.second, bad_rate,
                                           good_rate, format));
    }
  }
}

TEST(SincSamplerTest, Construction_UnsupportedChannelConfig) {
  for (auto bad_channel_config : kUnsupportedChannelConfigs) {
    // Use rates and formats that are known-good.
    auto source_rate = kFrameRates[0];
    auto dest_rate = source_rate;
    auto format = fuchsia::media::AudioSampleFormat::SIGNED_16;

    SCOPED_TRACE(testing::Message() << "Chans " << bad_channel_config.first << ">"
                                    << bad_channel_config.second << ", rates " << source_rate << ":"
                                    << dest_rate << ", format " << static_cast<uint64_t>(format));
    EXPECT_EQ(nullptr, SelectSincSampler(bad_channel_config.first, bad_channel_config.second,
                                         source_rate, dest_rate, format));

    source_rate = kFrameRates[std::size(kFrameRates) - 1];
    dest_rate = source_rate;
    format = fuchsia::media::AudioSampleFormat::FLOAT;

    SCOPED_TRACE(testing::Message() << "Chans " << bad_channel_config.first << ">"
                                    << bad_channel_config.second << ", rates " << source_rate << ":"
                                    << dest_rate << ", format " << static_cast<uint64_t>(format));
    EXPECT_EQ(nullptr, SelectSincSampler(bad_channel_config.first, bad_channel_config.second,
                                         source_rate, dest_rate, format));
  }
}

TEST(SincSamplerTest, Construction_UnsupportedFormat) {
  // Use channel configs and rates that are known-good.
  auto channel_config = kChannelConfigs[0];
  auto source_rate = kFrameRates[0];
  auto dest_rate = source_rate;

  // bad format: one more than the last enum
  auto bad_format = kInvalidFormat;
  SCOPED_TRACE(testing::Message() << "Chans " << channel_config.first << ">"
                                  << channel_config.second << ", rates " << source_rate << ":"
                                  << dest_rate << ", format " << static_cast<uint64_t>(bad_format));
  EXPECT_EQ(nullptr, SelectSincSampler(channel_config.first, channel_config.second, source_rate,
                                       dest_rate, bad_format));
}

// Test that position advances as it should
TEST(SincSamplerTest, SamplingPosition_Basic) {
  auto mixer = SelectSincSampler(1, 1, 48000, 48000, fuchsia::media::AudioSampleFormat::FLOAT);

  EXPECT_EQ(mixer->pos_filter_width().raw_value(), kSincFilterSideLength - 1);
  EXPECT_EQ(mixer->neg_filter_width().raw_value(), kSincFilterSideLength - 1);

  bool do_not_accum = false;
  bool source_is_consumed;
  float source[] = {1.0f,  2.0f,  3.0f,  4.0f,  5.0f,  6.0f,  7.0f,  8.0f,  9.0f,  10.0f,
                    11.0f, 12.0f, 13.0f, 14.0f, 15.0f, 16.0f, 17.0f, 18.0f, 19.0f, 20.0f};
  const int64_t source_frames = std::size(source);
  Fixed source_offset = ffl::FromRatio(3, 4);

  float dest[40];
  const uint32_t dest_frames = std::size(dest);
  uint32_t dest_offset = 0;

  // Pass in 20 frames
  source_is_consumed = mixer->Mix(dest, dest_frames, &dest_offset, source, source_frames,
                                  &source_offset, do_not_accum);
  EXPECT_TRUE(source_is_consumed);
  EXPECT_GE(source_offset + mixer->pos_filter_width(), Fixed(source_frames));
  EXPECT_EQ(dest_offset, source_offset.Floor());
}

// Validate the "seam" between buffers, at unity rate-conversion
TEST(SincSamplerTest, SamplingValues_DC_Unity) {
  constexpr uint32_t kSourceRate = 44100;
  constexpr uint32_t kDestRate = 44100;
  auto mixer =
      SelectSincSampler(1, 1, kSourceRate, kDestRate, fuchsia::media::AudioSampleFormat::FLOAT);

  bool do_not_accum = false;
  bool source_is_consumed;

  constexpr uint32_t kDestLen = 512;
  uint32_t dest_offset = 0;
  auto dest = std::make_unique<float[]>(kDestLen);

  constexpr uint32_t kSourceLen = kDestLen / 2;
  auto source_offset = Fixed(0);
  auto source = std::make_unique<float[]>(kSourceLen);
  for (auto idx = 0u; idx < kSourceLen; ++idx) {
    source[idx] = 1.0f;
  }

  auto& info = mixer->bookkeeping();
  info.step_size = kOneFrame;

  // Mix the first half of the destination
  source_is_consumed = mixer->Mix(dest.get(), kDestLen, &dest_offset, source.get(), kSourceLen,
                                  &source_offset, do_not_accum);
  EXPECT_TRUE(source_is_consumed) << std::hex << source_offset.raw_value();
  EXPECT_GE(source_offset + mixer->pos_filter_width(), Fixed(kSourceLen));
  EXPECT_EQ(source_offset.Floor(), dest_offset);
  auto first_half_dest = dest_offset;

  // Now mix the rest
  source_offset -= Fixed(kSourceLen);
  source_is_consumed = mixer->Mix(dest.get(), kDestLen, &dest_offset, source.get(), kSourceLen,
                                  &source_offset, do_not_accum);
  EXPECT_TRUE(source_is_consumed) << std::hex << source_offset.raw_value();
  EXPECT_GE(source_offset + mixer->pos_filter_width(), Fixed(kSourceLen));

  // The "seam" between buffers should be invisible
  for (auto idx = first_half_dest - 2; idx < first_half_dest + 2; ++idx) {
    EXPECT_NEAR(dest[idx], 1.0f, 0.001f);
  }
}

// Validate the "seam" between buffers, while down-sampling
TEST(SincSamplerTest, SamplingValues_DC_DownSample) {
  constexpr uint32_t kSourceRate = 48000;
  constexpr uint32_t kDestRate = 44100;
  auto mixer =
      SelectSincSampler(1, 1, kSourceRate, kDestRate, fuchsia::media::AudioSampleFormat::FLOAT);

  bool do_not_accum = false;
  bool source_is_consumed;

  constexpr uint32_t kDestLen = 512;
  uint32_t dest_offset = 0;
  auto dest = std::make_unique<float[]>(kDestLen);

  constexpr uint32_t kSourceLen = kDestLen / 2;
  auto source_offset = Fixed(0);
  auto source = std::make_unique<float[]>(kSourceLen);
  for (auto idx = 0u; idx < kSourceLen; ++idx) {
    source[idx] = 1.0f;
  }

  auto& info = mixer->bookkeeping();
  info.step_size = (kOneFrame * kSourceRate) / kDestRate;
  info.SetRateModuloAndDenominator(
      Fixed(kOneFrame * kSourceRate - info.step_size * kDestRate).raw_value(), kDestRate);
  info.source_pos_modulo = 0;

  // Mix the first half of the destination
  source_is_consumed = mixer->Mix(dest.get(), kDestLen, &dest_offset, source.get(), kSourceLen,
                                  &source_offset, do_not_accum);
  EXPECT_TRUE(source_is_consumed) << std::hex << source_offset.raw_value();
  EXPECT_GE(source_offset + mixer->pos_filter_width(), Fixed(kSourceLen));
  auto first_half_dest = dest_offset;

  // Now mix the rest
  source_offset -= Fixed(kSourceLen);
  source_is_consumed = mixer->Mix(dest.get(), kDestLen, &dest_offset, source.get(), kSourceLen,
                                  &source_offset, do_not_accum);
  EXPECT_TRUE(source_is_consumed) << std::hex << source_offset.raw_value();
  EXPECT_GE(source_offset + mixer->pos_filter_width(), Fixed(kSourceLen));

  // The "seam" between buffers should be invisible
  for (auto idx = first_half_dest - 2; idx < first_half_dest + 2; ++idx) {
    EXPECT_NEAR(dest[idx], 1.0f, 0.001f);
  }
}

// Validate the "seam" between buffers, while up-sampling
TEST(SincSamplerTest, SamplingValues_DC_UpSample) {
  constexpr uint32_t kSourceRate = 12000;
  constexpr uint32_t kDestRate = 48000;
  auto mixer =
      SelectSincSampler(1, 1, kSourceRate, kDestRate, fuchsia::media::AudioSampleFormat::FLOAT);

  bool do_not_accum = false;
  bool source_is_consumed;

  constexpr uint32_t kDestLen = 1024;
  uint32_t dest_offset = 0;
  auto dest = std::make_unique<float[]>(kDestLen);

  constexpr uint32_t kSourceLen = kDestLen / 8;
  auto source_offset = Fixed(0);
  auto source = std::make_unique<float[]>(kSourceLen);
  for (auto idx = 0u; idx < kSourceLen; ++idx) {
    source[idx] = 1.0f;
  }

  auto& info = mixer->bookkeeping();
  info.step_size = (kOneFrame * kSourceRate) / kDestRate;
  info.SetRateModuloAndDenominator(
      kOneFrame.raw_value() * kSourceRate - info.step_size.raw_value() * kDestRate, kDestRate);
  info.source_pos_modulo = 0;

  // Mix the first half of the destination
  source_is_consumed = mixer->Mix(dest.get(), kDestLen / 2, &dest_offset, source.get(), kSourceLen,
                                  &source_offset, do_not_accum);
  EXPECT_TRUE(source_is_consumed) << std::hex << source_offset.raw_value();
  EXPECT_GE(source_offset + mixer->pos_filter_width(), Fixed(kSourceLen));
  EXPECT_EQ(Fixed(source_offset * 4).Floor(), dest_offset);
  auto first_half_dest = dest_offset;

  // Now mix the rest
  source_offset -= Fixed(kSourceLen);
  source_is_consumed = mixer->Mix(dest.get(), kDestLen, &dest_offset, source.get(), kSourceLen,
                                  &source_offset, do_not_accum);
  EXPECT_TRUE(source_is_consumed) << std::hex << source_offset.raw_value();
  EXPECT_GE(source_offset + mixer->pos_filter_width(), Fixed(kSourceLen));

  // The two samples before and after the "seam" between buffers should be invisible
  for (auto idx = first_half_dest - 2; idx < first_half_dest + 2; ++idx) {
    EXPECT_NEAR(dest[idx], 1.0f, 0.001f);
  }
}

// Based on sinc coefficients and our arbitrary near-zero source position of -1/128.
constexpr float kSource[] = {
    1330.113831,                                             // These values were chosen so
    -1330.113831, 1330.113831,  -1330.113831, 1330.113831,   // that if these first 13 values
    -1330.113831, 1330.113831,  -1330.113831, 1330.113831,   // are ignored, we expect
    -1330.113831, 1330.113831,  -1330.113831, 1330.113831,   // kValueWithoutPreviousFrames
    -10.0010101,                                             //
    268.8840529,  -268.8840529, 268.8840529,  -268.8840529,  // If they are NOT ignored,
    268.8840529,  -268.8840529, 268.8840529,  -268.8840529,  // then we expect the result
    268.8840529,  -268.8840529, 268.8840529,  -268.8840529,  // kValueWithPreviousFrames.
    268.8840529,
};
constexpr Fixed kMixOneFrameSourceOffset = ffl::FromRatio(1, 128);
constexpr float kValueWithoutPreviousFrames = -15.0;
constexpr float kValueWithPreviousFrames = 10.0;

// Mix a single frame of output based on kSource[0]. Producing a frame for position 0 requires
// neg_width previous frames, kSource[0] itself, and pos_width frames beyond kSource[0].
float MixOneFrame(std::unique_ptr<Mixer>& mixer, Fixed source_offset) {
  auto neg_width = mixer->neg_filter_width().Floor();
  auto pos_width = mixer->pos_filter_width().Floor();
  EXPECT_NE(Fixed(pos_width).raw_value() + 1, mixer->neg_filter_width().raw_value())
      << "This test assumes SincSampler is symmetric, and that negative width includes a "
         "fraction";

  float dest;
  uint32_t dest_offset = 0;
  int64_t source_frames = pos_width + 1;

  bool source_is_consumed = mixer->Mix(&dest, 1, &dest_offset, &(kSource[neg_width]), source_frames,
                                       &source_offset, false);
  EXPECT_TRUE(source_is_consumed);
  EXPECT_EQ(dest_offset, 1u) << "No output frame was produced";

  return dest;
}

// Mix a single frame, without any previously-cached data.
TEST(SincSamplerTest, SamplingValues_MixOne_NoCache) {
  auto mixer = SelectSincSampler(1, 1, 44100, 44100, fuchsia::media::AudioSampleFormat::FLOAT);

  // Mix a single frame at approx position 0. (We use a slightly non-zero value because at true 0,
  // only source[0] itself is used anyway.) In this case we have not provided previous frames.
  float dest = MixOneFrame(mixer, -kMixOneFrameSourceOffset);

  // If we incorrectly shifted/retained even a single frame of the above data, this won't match.
  EXPECT_FLOAT_EQ(dest, kValueWithoutPreviousFrames);
}

// Mix a single frame, with previously-cached data.
TEST(SincSamplerTest, SamplingValues_MixOne_WithCache) {
  auto mixer = SelectSincSampler(1, 1, 44100, 44100, fuchsia::media::AudioSampleFormat::FLOAT);
  auto neg_width = mixer->neg_filter_width().Floor();

  // Now, populate the cache with previous frames, instead of using default (silence) values.
  // The outparam value of source_offset tells us the cache is populated with neg_width frames,
  // which is ideal for mixing a subsequent source buffer starting at source position [0].
  float dest;
  uint32_t dest_offset = 0;
  auto source_frames = neg_width;
  Fixed source_offset = Fixed(source_frames) - kMixOneFrameSourceOffset;

  bool source_is_consumed =
      mixer->Mix(&dest, 1, &dest_offset, &(kSource[0]), source_frames, &source_offset, false);
  EXPECT_TRUE(source_is_consumed);
  EXPECT_EQ(source_offset, Fixed(source_frames) - kMixOneFrameSourceOffset);
  EXPECT_EQ(dest_offset, 0u) << "Unexpectedly produced output " << dest;

  // Mix a single frame at approx position 0. (We use a slightly non-zero value because at true 0,
  // only the frame value itself is used anyway.) In this case we HAVE provided previous frames.
  dest = MixOneFrame(mixer, -kMixOneFrameSourceOffset);

  // If we incorrectly shifted/retained even a single frame of the above data, this won't match.
  EXPECT_FLOAT_EQ(dest, kValueWithPreviousFrames);
}

// Mix a single frame, after feeding the cache with previous data, one frame at a time.
// Specifying source_offset >= 0 guarantees that the cached source data will be shifted
// appropriately, so that subsequent Mix() calls can correctly use that data.
TEST(SincSamplerTest, SamplingValues_MixOne_CachedFrameByFrame) {
  auto mixer = SelectSincSampler(1, 1, 44100, 44100, fuchsia::media::AudioSampleFormat::FLOAT);
  auto neg_width = mixer->neg_filter_width().Floor();

  // Now, populate the cache with previous data, one frame at a time.
  float dest;
  uint32_t dest_offset = 0;
  const auto source_frames = 1;
  Fixed source_offset = Fixed(source_frames) - kMixOneFrameSourceOffset;

  for (auto neg_idx = 0u; neg_idx < neg_width; ++neg_idx) {
    bool source_is_consumed = mixer->Mix(&dest, 1, &dest_offset, &(kSource[neg_idx]), source_frames,
                                         &source_offset, false);
    EXPECT_TRUE(source_is_consumed);
    EXPECT_EQ(source_offset, Fixed(source_frames) - kMixOneFrameSourceOffset);
    EXPECT_EQ(dest_offset, 0u) << "Unexpectedly produced output " << dest;
  }

  // Mix a single frame at approx position 0. (We use a slightly non-zero value because at true 0,
  // only the frame value itself is used anyway.) In this case we HAVE provided previous frames.
  dest = MixOneFrame(mixer, -kMixOneFrameSourceOffset);

  // If we incorrectly shifted/retained even a single frame of the above data, this won't match.
  EXPECT_FLOAT_EQ(dest, kValueWithPreviousFrames);
}

}  // namespace
}  // namespace media::audio::mixer

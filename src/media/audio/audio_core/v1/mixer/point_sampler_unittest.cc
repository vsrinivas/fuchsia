// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/v1/mixer/point_sampler.h"

#include <fbl/algorithm.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/format2/channel_mapper.h"
#include "src/media/audio/lib/format2/sample_converter.h"
#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/lib/timeline/timeline_rate.h"

namespace media::audio::mixer {
namespace {

using testing::FloatEq;
using testing::Pointwise;

using Resampler = ::media::audio::Mixer::Resampler;

constexpr auto kMaxInt24In32 = media_audio::kMaxInt24In32;
constexpr auto kMinInt24In32 = media_audio::kMinInt24In32;

// TODO(fxbug.dev/70578): Relocate position-related tests here, from audio_fidelity_tests
// TODO(fxbug.dev/70580): Refactor the set of pass-thru, rechannel, accumulate and gain unittests,
// so they run on SincSampler as well  (perhaps moving them into a mixer_unittest.cc).
class PointSamplerTest : public testing::Test {
 protected:
  static const std::vector<int32_t> kFrameRates;
  static const std::vector<int32_t> kUnsupportedFrameRates;

  static const std::vector<std::pair<int32_t, int32_t>> kChannelConfigs;
  static const std::vector<std::pair<int32_t, int32_t>> kUnsupportedChannelConfigs;

  static const std::vector<fuchsia::media::AudioSampleFormat> kFormats;
  const fuchsia::media::AudioSampleFormat kInvalidFormat =
      static_cast<fuchsia::media::AudioSampleFormat>(
          static_cast<int64_t>(kFormats[std::size(kFormats) - 1]) + 1);

  std::unique_ptr<Mixer> SelectPointSampler(
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

    return Mixer::Select(source_stream_type, dest_stream_type, Mixer::Resampler::SampleAndHold);
  }

  // When we specify source data in uint8/int16/int32 formats, it improves readability to specify
  // expected values in that format as well. The expected array itself is float[], so we use this
  // function to shift values expressed as uint8, int16, etc., into the [-1.0, 1.0] float range.
  //
  // Note: 'shift_by' values must be 1 less than might seem obvious, to account for the sign bit.
  // E.g.: to shift int16 values -0x8000 and 0x7FFF into float range, shift_by must be 15 (not 16).
  void ShiftRightBy(std::vector<float>& floats, uint32_t shift_by) {
    for (float& val : floats) {
      for (auto shift_num = 0u; shift_num < shift_by; ++shift_num) {
        val *= 0.5f;
      }
    }
  }

  // Use the supplied mixer to mix without SRC. Assumes no accumulation, but can be overridden.
  // Used by tests that do simple mixing and need not inspect the returned position values.
  void DoMix(Mixer* mixer, const void* source_buf, float* accum_buf, bool accumulate,
             int64_t num_frames, float gain_db = media_audio::kUnityGainDb) {
    ASSERT_NE(mixer, nullptr);

    int64_t dest_offset = 0;
    auto source_offset = Fixed(0);

    mixer->gain.SetSourceGain(gain_db);
    mixer->Mix(accum_buf, num_frames, &dest_offset, source_buf, num_frames, &source_offset,
               accumulate);

    EXPECT_EQ(dest_offset, num_frames);
    EXPECT_EQ(source_offset, Fixed(num_frames));
  }
};

const std::vector<int32_t> PointSamplerTest::kFrameRates = {
    8000,  11025, 16000, 22050, 24000,  32000,
    44100, 48000, 88200, 96000, 176400, fuchsia::media::MAX_PCM_FRAMES_PER_SECOND,
};

const std::vector<int32_t> PointSamplerTest::kUnsupportedFrameRates = {
    fuchsia::media::MIN_PCM_FRAMES_PER_SECOND - 1,
    fuchsia::media::MAX_PCM_FRAMES_PER_SECOND + 1,
};

const std::vector<std::pair<int32_t, int32_t>> PointSamplerTest::kChannelConfigs = {
    {1, 1}, {1, 2}, {1, 3}, {1, 4},  //
    {2, 1}, {2, 2}, {2, 3}, {2, 4},  //
    {3, 1}, {3, 2}, {3, 3},          //
    {4, 1}, {4, 2}, {4, 4},          //
    {5, 5}, {6, 6}, {7, 7}, {8, 8},
};

const std::vector<std::pair<int32_t, int32_t>> PointSamplerTest::kUnsupportedChannelConfigs = {
    {1, 5}, {1, 8}, {1, 9},  // Unsupported channel
    {2, 5}, {2, 8}, {2, 9},  // configurations --
    {3, 5}, {3, 8}, {3, 9},  // maximum number of
    {4, 5}, {4, 7}, {4, 9},  // channels is 8.
    {5, 1}, {9, 1}, {9, 9},
};

const std::vector<fuchsia::media::AudioSampleFormat> PointSamplerTest::kFormats = {
    fuchsia::media::AudioSampleFormat::UNSIGNED_8,
    fuchsia::media::AudioSampleFormat::SIGNED_16,
    fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32,
    fuchsia::media::AudioSampleFormat::FLOAT,
};

// These formats are supported
TEST_F(PointSamplerTest, Construction) {
  for (auto channel_config : kChannelConfigs) {
    for (auto rate : kFrameRates) {
      for (auto format : kFormats) {
        auto mixer =
            SelectPointSampler(channel_config.first, channel_config.second, rate, rate, format);

        SCOPED_TRACE(testing::Message() << "Chans " << channel_config.first << ">"
                                        << channel_config.second << ", rates " << rate << ":"
                                        << rate << ", format " << static_cast<int64_t>(format));
        EXPECT_NE(mixer, nullptr);
      }
    }
  }
}

// Rate mismatch is unsupported
TEST_F(PointSamplerTest, ConstructionDifferingRates) {
  for (auto source_rate : kFrameRates) {
    for (auto dest_rate : kFrameRates) {
      if (source_rate == dest_rate) {
        continue;
      }

      // Use channel configs and formats that are known-good.
      auto channel_config = kChannelConfigs[0];
      auto format = kFormats[0];

      SCOPED_TRACE(testing::Message() << "Chans " << channel_config.first << ">"
                                      << channel_config.second << ", rates " << source_rate << ":"
                                      << dest_rate << ", format " << static_cast<int64_t>(format));
      EXPECT_EQ(nullptr, SelectPointSampler(channel_config.first, channel_config.second,
                                            source_rate, dest_rate, format));

      channel_config = kChannelConfigs[std::size(kChannelConfigs) - 1];
      format = kFormats[std::size(kFormats) - 1];

      SCOPED_TRACE(testing::Message() << "Chans " << channel_config.first << ">"
                                      << channel_config.second << ", rates " << source_rate << ":"
                                      << dest_rate << ", format " << static_cast<int64_t>(format));
      EXPECT_EQ(nullptr, SelectPointSampler(channel_config.first, channel_config.second,
                                            source_rate, dest_rate, format));
    }
  }
}

// Out-of-range rates are unsupported
TEST_F(PointSamplerTest, ConstructionUnsupportedRate) {
  for (auto bad_rate : kUnsupportedFrameRates) {
    // Use channel configs and formats that are known-good.
    auto channel_config = kChannelConfigs[0];
    auto format = kFormats[0];

    SCOPED_TRACE(testing::Message()
                 << "Chans " << channel_config.first << ">" << channel_config.second << ", rates "
                 << bad_rate << ":" << bad_rate << ", format " << static_cast<int64_t>(format));
    EXPECT_EQ(nullptr, SelectPointSampler(channel_config.first, channel_config.second, bad_rate,
                                          bad_rate, format));

    channel_config = kChannelConfigs[std::size(kChannelConfigs) - 1];
    format = kFormats[std::size(kFormats) - 1];

    SCOPED_TRACE(testing::Message()
                 << "Chans " << channel_config.first << ">" << channel_config.second << ", rates "
                 << bad_rate << ":" << bad_rate << ", format " << static_cast<int64_t>(format));
    EXPECT_EQ(nullptr, SelectPointSampler(channel_config.first, channel_config.second, bad_rate,
                                          bad_rate, format));
  }
}

// These channel configs are unsupported
TEST_F(PointSamplerTest, ConstructionUnsupportedChannelConfig) {
  for (auto bad_channel_config : kUnsupportedChannelConfigs) {
    // Use rates and formats that are known-good.
    auto rate = kFrameRates[0];
    auto format = fuchsia::media::AudioSampleFormat::SIGNED_16;

    SCOPED_TRACE(testing::Message() << "Chans " << bad_channel_config.first << ">"
                                    << bad_channel_config.second << ", rates " << rate << ":"
                                    << rate << ", format " << static_cast<int64_t>(format));
    EXPECT_EQ(nullptr, SelectPointSampler(bad_channel_config.first, bad_channel_config.second, rate,
                                          rate, format));

    rate = kFrameRates[std::size(kFrameRates) - 1];
    format = fuchsia::media::AudioSampleFormat::FLOAT;

    SCOPED_TRACE(testing::Message() << "Chans " << bad_channel_config.first << ">"
                                    << bad_channel_config.second << ", rates " << rate << ":"
                                    << rate << ", format " << static_cast<int64_t>(format));
    EXPECT_EQ(nullptr, SelectPointSampler(bad_channel_config.first, bad_channel_config.second, rate,
                                          rate, format));
  }
}

// This format is unsupported
TEST_F(PointSamplerTest, ConstructionUnsupportedFormat) {
  // Use channel configs and rates that are known-good.
  auto channel_config = kChannelConfigs[0];
  auto rate = kFrameRates[0];

  // bad format: one more than the last enum
  auto bad_format = kInvalidFormat;

  SCOPED_TRACE(testing::Message() << "Chans " << channel_config.first << ">"
                                  << channel_config.second << ", rates " << rate << ":" << rate
                                  << ", format " << static_cast<uint64_t>(bad_format));
  EXPECT_EQ(nullptr, SelectPointSampler(channel_config.first, channel_config.second, rate, rate,
                                        bad_format));
}

// PassThru - can audio data flow through a Mix() call without change, in various configurations?
//
class PointSamplerPassThruTest : public PointSamplerTest {};

// Can 8-bit values flow unchanged (1-1, N-N) thru the system? With 1:1 frame
// conversion, unity scale and no accumulation, we expect bit-equality.
TEST_F(PointSamplerPassThruTest, Uint8) {
  auto source = std::vector<uint8_t>{0x00, 0xFF, 0x27, 0xCD, 0x7F, 0x80, 0xA6, 0x6D};

  auto accum = std::vector<float>(source.size());
  auto expect = std::vector<float>(source.size());
  for (auto idx = 0u; idx < std::size(source); ++idx) {
    expect[idx] = static_cast<float>(source[idx]) / 0x80 - 1.0f;
  }

  // Try in 1-channel mode
  auto mixer =
      SelectPointSampler(1, 1, 48000, 48000, fuchsia::media::AudioSampleFormat::UNSIGNED_8);
  DoMix(mixer.get(), source.data(), accum.data(), false, accum.size());
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));

  // Now try in 8-channel mode
  std::fill(accum.begin(), accum.end(), 0xB4);  // fill accum with nonsense (to be overwritten)
  mixer = SelectPointSampler(8, 8, 32000, 32000, fuchsia::media::AudioSampleFormat::UNSIGNED_8);
  DoMix(mixer.get(), source.data(), accum.data(), false, accum.size() / 8);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Can 16-bit values flow unchanged (2-2, N-N) thru the system? With 1:1 frame
// conversion, unity scale and no accumulation, we expect bit-equality.
TEST_F(PointSamplerPassThruTest, Int16) {
  auto source = std::vector<int16_t>{-0x8000, 0x7FFF, -0x67A7, 0x4D4D, -0x123, 0, 0x2600, -0x2DCB};

  auto accum = std::vector<float>(source.size());
  auto expect = std::vector<float>(source.size());
  std::copy(source.begin(), source.end(), expect.begin());
  ShiftRightBy(expect, 15);

  // Try in 2-channel mode
  auto mixer = SelectPointSampler(2, 2, 48000, 48000, fuchsia::media::AudioSampleFormat::SIGNED_16);
  DoMix(mixer.get(), source.data(), accum.data(), false, accum.size() / 2);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));

  std::fill(accum.begin(), accum.end(), 0xF00D);  // fill accum with nonsense (to be overwritten)
  // Now try in 4-channel mode
  mixer = SelectPointSampler(4, 4, 192000, 192000, fuchsia::media::AudioSampleFormat::SIGNED_16);
  DoMix(mixer.get(), source.data(), accum.data(), false, accum.size() / 4);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Can 24-bit values flow unchanged (2-2, N-N) thru the system? With 1:1 frame
// conversion, unity scale and no accumulation, we expect bit-equality.
TEST_F(PointSamplerPassThruTest, Int24In32) {
  auto source =
      std::vector<int32_t>{kMinInt24In32, kMaxInt24In32, -0x67A7E700, 0x4D4D4D00, -0x1234500, 0,
                           0x26006200,    -0x2DCBA900};

  auto accum = std::vector<float>(source.size());
  auto expect = std::vector<float>(source.size());
  std::copy(source.begin(), source.end(), expect.begin());
  ShiftRightBy(expect, 31);

  // Try in 2-channel mode
  auto mixer =
      SelectPointSampler(2, 2, 48000, 48000, fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32);
  DoMix(mixer.get(), source.data(), accum.data(), false, accum.size() / 2);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));

  std::fill(accum.begin(), accum.end(), 0xBADF00D);  // fill accum with nonsense (to be overwritten)
  // Now try in 8-channel mode
  mixer =
      SelectPointSampler(8, 8, 96000, 96000, fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32);
  DoMix(mixer.get(), source.data(), accum.data(), false, accum.size() / 8);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Can float values flow unchanged (1-1, N-N) thru the system? With 1:1 frame
// conversion, unity scale and no accumulation, we expect bit-equality.
TEST_F(PointSamplerPassThruTest, Float) {
  auto source = std::vector<float>{-1.0, 1.0f,      -0.809783935f, 0.603912353f, -0.00888061523f,
                                   0.0f, 0.296875f, -0.357757568f};

  // Try in 1-channel mode
  auto accum = std::vector<float>(source.size());
  auto mixer = SelectPointSampler(1, 1, 48000, 48000, fuchsia::media::AudioSampleFormat::FLOAT);
  DoMix(mixer.get(), source.data(), accum.data(), false, accum.size());
  EXPECT_THAT(accum, Pointwise(FloatEq(), source));

  // Now try in 4-channel mode
  std::fill(accum.begin(), accum.end(), NAN);  // fill accum with nonsense (overwritten)
  mixer = SelectPointSampler(4, 4, 8000, 8000, fuchsia::media::AudioSampleFormat::FLOAT);
  DoMix(mixer.get(), source.data(), accum.data(), false, accum.size() / 4);
  EXPECT_THAT(accum, Pointwise(FloatEq(), source));
}

// Rechannelization tests
//
// Do we map source channels to destination channels correctly, in the overall mixer context?
class PointSamplerRechannelTest : public PointSamplerTest {};

// Are all valid data values passed correctly to 16-bit outputs for the 1->2 channel mapping.
TEST_F(PointSamplerRechannelTest, MonoToStereo) {
  auto source = std::vector<int16_t>{-0x08000, -0x3FFF, -1, 0, 1, 0x7FFF};

  auto accum = std::vector<float>(source.size() * 2);
  auto expect = std::vector<float>(source.size() * 2);
  for (auto idx = 0u; idx < source.size(); ++idx) {
    expect[idx * 2] = source[idx];
    expect[idx * 2 + 1] = source[idx];
  }
  ShiftRightBy(expect, 15);

  auto mixer = SelectPointSampler(1, 2, 48000, 48000, fuchsia::media::AudioSampleFormat::SIGNED_16);
  DoMix(mixer.get(), source.data(), accum.data(), false, accum.size() / 2);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Validate that we correctly mix stereo->mono, including precision below the source data format.
// The two samples in each input frame should be averaged, for each single-sample output frame.
// This includes resolution below what can be expressed with the 16-bit source format.
TEST_F(PointSamplerRechannelTest, StereoToMono) {
  auto source = std::vector<int16_t>{
      0,       0,        // Various values ...
      0x1,     -0x1,     // ... that sum ...
      -0x1357, 0x1357,   // ... to zero.
      -0x1111, 0x3333,   // positive even sum
      -0x5555, 0x1111,   // negative even sum
      -0x0001, 0x0006,   // positive odd sum - the ".5" result shouldn't be lost
      -0x2005, 0x2000,   // negative odd sum - the ".5" result shouldn't be lost
      0x7FFF,  0x7FFF,   // positive limit
      -0x8000, -0x8000,  // negative limit
  };

  auto accum =  // overwritten
      std::vector<float>{-0x1234, 0x4321, -0x13579, 0xC0FF, -0xAAAA, 0x555, 0xABC, 0x42, 0xD00D};
  auto expect = std::vector<float>{0, 0, 0, 0x1111, -0x2222, 2.5, -2.5, 0x7FFF, -0x8000};
  ShiftRightBy(expect, 15);  // right-shift these int16 values into float range

  auto mixer = SelectPointSampler(2, 1, 48000, 48000, fuchsia::media::AudioSampleFormat::SIGNED_16);
  DoMix(mixer.get(), source.data(), accum.data(), false, accum.size());
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Validate that we correctly mix quad->mono, including precision beyond the source format.
// The four samples in each input frame should be averaged, for each single-sample output frame.
// This includes resolution below what can be expressed with the 24-bit source format.
TEST_F(PointSamplerRechannelTest, QuadToMono) {
  auto source = std::vector<int32_t>{
      // clang-format off
       0x00000100,           0,              0,              0,       // should become 0.25
      -0x00000100,           0,              0,              0,       // should become -0.25
       0x00000100,    0x00000100,     0x00000100,            0,       // should become 0.75
      -0x00000100,   -0x00000100,    -0x00000100,            0,       // should become -0.75
       kMinInt24In32, kMinInt24In32,  kMinInt24In32,  kMinInt24In32,  // should become kMinInt32In32
       kMaxInt24In32, kMaxInt24In32,  kMaxInt24In32,  kMaxInt24In32,  // should become kMaxInt24In32
       kMaxInt24In32, kMaxInt24In32, -kMaxInt24In32, -kMaxInt24In32,  // should become 0
      // clang-format on
  };

  // Express expected values as "int24" (not int32) to clearly show fractional and min/max values.
  auto accum = std::vector<float>(source.size() / 4);
  std::vector<float> expect;
  if constexpr (media_audio::kEnable4ChannelWorkaround) {
    // For now, 4->1 just ignores channels 2 & 3.
    // TODO(fxbug.dev/85201): Remove this workaround, once the device properly maps channels.
    expect = {
        // clang-format off
              0.5,
             -0.5,
              1.0,
             -1.0,
      -0x800000,
       0x7FFFFF,
       0x7FFFFF,
        // clang-format on
    };
  } else {
    expect = {
        // clang-format off
              0.25,
             -0.25,
              0.75,
             -0.75,
      -0x800000,
       0x7FFFFF,
              0,
        // clang-format on
    };
  }
  ShiftRightBy(expect, 23);  // right-shift these "int24" values into float range

  auto mixer =
      SelectPointSampler(4, 1, 64000, 64000, fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32);
  DoMix(mixer.get(), source.data(), accum.data(), false, accum.size());
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Validate quad->stereo mixing, including sub-format precision. Note: 0|1|2|3 becomes 0+2 | 1+3
TEST_F(PointSamplerRechannelTest, QuadToStereo) {
  auto source = std::vector<int32_t>{
      // clang-format off
      0x00000100,   -0x00000100,           0,             0,      // [0,2]=>0.5,  [1,3]=>-0.5
      kMinInt24In32, kMaxInt24In32, kMinInt24In32, kMaxInt24In32, // [0,2]=>kMin, [1,3]=>kMax
      kMaxInt24In32,        0,     -kMaxInt24In32,        0,      // [0,2]=>0,    [1,3]=>0
      // clang-format on
  };

  // Will be overwritten
  auto accum = std::vector<float>{-0x1234, 0x4321, -0x13579, 0xC0FF, -0xAAAA, 0x555};

  // Express expected values as "int24" (not int32) to clearly show fractional and min/max values.
  std::vector<float> expect;
  if constexpr (media_audio::kEnable4ChannelWorkaround) {
    // For now, 4->2 just ignores channels 2 & 3.
    // TODO(fxbug.dev/85201): Remove this workaround, once the device properly maps channels.
    expect = {1, -1, -0x800000, 0x7FFFFF, 0x7FFFFF, 0};
  } else {
    expect = {0.5, -0.5, -0x800000, 0x7FFFFF, 0, 0};
  }

  ShiftRightBy(expect, 23);  // right-shift these "int24" values into float range

  auto mixer =
      SelectPointSampler(4, 2, 22050, 22050, fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32);
  DoMix(mixer.get(), source.data(), accum.data(), false,
        accum.size() / 2);  // dest frames have 2 samples each
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Are all valid data values passed correctly to 16-bit outputs for the 1->4 channel mapping?
TEST_F(PointSamplerRechannelTest, MonoToQuad) {
  auto source = std::vector<int16_t>{-0x8000, -0x3FFF, -1, 0, 1, 0x7FFF};

  auto accum = std::vector<float>(source.size() * 4);
  auto expect = std::vector<float>(source.size() * 4);
  for (auto idx = 0u; idx < source.size(); ++idx) {
    expect[idx * 4] = source[idx];
    expect[idx * 4 + 1] = source[idx];
    expect[idx * 4 + 2] = source[idx];
    expect[idx * 4 + 3] = source[idx];
  }
  ShiftRightBy(expect, 15);  // right-shift these int16 values into float range

  auto mixer = SelectPointSampler(1, 4, 48000, 48000, fuchsia::media::AudioSampleFormat::SIGNED_16);
  DoMix(mixer.get(), source.data(), accum.data(), false, accum.size() / 4);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Are all valid data values passed correctly to 16-bit outputs for the 2->4 channel mapping?
// Here, we split a stereo source frame to quad output as [L, R, L, R].
TEST_F(PointSamplerRechannelTest, StereoToQuad) {
  // Input data in the [L, R] channelization -- arbitrary values in the 24-in-32 format
  auto source = std::vector<int32_t>{
      // clang-format off
       kMinInt24In32, -0x3FFFFF00,
      -0x00000100,            0,
       0x00000100,     kMaxInt24In32,
      // clang-format on
  };

  auto accum = std::vector<float>(source.size() * 2);
  auto expect = std::vector<float>(source.size() * 2);
  for (auto idx = 0u; idx < source.size(); idx += 2) {
    expect[idx * 2] = static_cast<float>(source[idx]);          // First sample should be L
    expect[idx * 2 + 1] = static_cast<float>(source[idx + 1]);  // Second sample should be R
    expect[idx * 2 + 2] = static_cast<float>(source[idx]);      // Third  sample should be L
    expect[idx * 2 + 3] = static_cast<float>(source[idx + 1]);  // Fourth sample should be R
  }
  ShiftRightBy(expect, 31);  // right-shift these int32 values into float range

  auto mixer =
      SelectPointSampler(2, 4, 48000, 48000, fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32);
  DoMix(mixer.get(), source.data(), accum.data(), false, accum.size() / 4);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Accumulate tests
//
// Can values in our multi-stream accumulator temporarily exceed the max or min values for an
// individual stream? What is our accumulator's limit; does it clamp or rollover?
//
class PointSamplerAccumulateTest : public PointSamplerTest {};

// Do we obey the 'accumulate' flag if mixing into existing accumulated data?
// The PassThru tests depend on accum FALSE working correctly: just validate TRUE here.
TEST_F(PointSamplerAccumulateTest, Basic) {
  auto source = std::vector<int16_t>{-0x1111, 0x3333, -0x6666, 0x4444};

  auto accum = std::vector<float>{0x5432, 0x1234, -0x0123, -0x3210};
  auto expect = std::vector<float>{0x4321, 0x4567, -0x6789, 0x1234};
  auto expect2 = std::vector<float>{0x3210, 0x789A, -0xCDEF, 0x5678};
  ShiftRightBy(accum, 15);
  ShiftRightBy(expect, 15);  // right-shift these int16 values into float range
  ShiftRightBy(expect2, 15);

  auto mixer = SelectPointSampler(2, 2, 48000, 48000, fuchsia::media::AudioSampleFormat::SIGNED_16);
  DoMix(mixer.get(), source.data(), accum.data(), true, accum.size() / 2);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));

  DoMix(mixer.get(), source.data(), accum.data(), true, accum.size() / 2);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect2));
}

// Can accumulator result exceed the max range of individual streams?
TEST_F(PointSamplerAccumulateTest, BeyondSourceLimit) {
  // When mixed 2x and 3x, these full-scale values far exceed any int16 range
  auto max_source = std::array<int16_t, 2>{0x7FFF, -0x8000};

  std::vector<float> accum(2);
  std::copy(max_source.begin(), max_source.end(), accum.begin());
  ShiftRightBy(accum, 15);

  std::vector<float> expect_double(2);
  std::vector<float> expect_triple(2);
  std::copy(accum.begin(), accum.end(), expect_double.begin());
  std::copy(accum.begin(), accum.end(), expect_triple.begin());
  for (auto idx = 0u; idx < accum.size(); ++idx) {
    expect_double[idx] *= 2.0f;
    expect_triple[idx] *= 3.0f;
  }

  // These values exceed the per-stream range of int16
  auto mixer = SelectPointSampler(1, 1, 48000, 48000, fuchsia::media::AudioSampleFormat::SIGNED_16);
  DoMix(mixer.get(), max_source.data(), accum.data(), true, accum.size());
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect_double));

  // These values even exceed uint16
  DoMix(mixer.get(), max_source.data(), accum.data(), true, accum.size());
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect_triple));
}

// As an optimization, mixers skip mixing altogether if the gain is below a certain mute-equivalent
// threshold. They do this even when "accumulate" is false (technically they should write silence).
// Validate the SampleAndHold interpolator for this behavior.
TEST_F(PointSamplerAccumulateTest, NoOpWhenMuted) {
  auto source = std::array<int16_t, 4>{-32768, 32767, -16384, 16383};

  auto accum = std::vector<float>(source.size());
  std::copy(source.begin(), source.end(), accum.begin());
  ShiftRightBy(accum, 15);

  auto expect = std::vector<float>(accum.size());
  std::copy(accum.begin(), accum.end(), expect.begin());

  auto mixer = SelectPointSampler(1, 1, 48000, 48000, fuchsia::media::AudioSampleFormat::SIGNED_16);
  // Use a gain guaranteed to silence any signal -- media_audio::kMinGainDb.
  DoMix(mixer.get(), source.data(), accum.data(), true, accum.size(), media_audio::kMinGainDb);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));

  // When accumulate = false but gain is sufficiently low, overwriting previous contents is skipped.
  // This should lead to the same results as above.
  DoMix(mixer.get(), source.data(), accum.data(), false, accum.size(), media_audio::kMinGainDb);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Data scaling tests
//
// These scaling tests involve gain or accumulation, in the context of mixing (as opposed to gain
// unittests that directly probe the Gain object in isolation).
//
class PointSamplerScalingTest : public PointSamplerTest {
 protected:
  float DbFromScale(float scale) { return 20.0f * log10(scale); }
};

// Validate data-scaling accuracy in PointSampler mixing, for scaling of exactly 10.0x and 0.25x.
TEST_F(PointSamplerScalingTest, Linearity) {
  auto source = std::vector<int16_t>{0x0CE4, 0x0CCC, 0x23, 4, -0x0E, -0x19, -0x0CCC, -0x0CDB};
  std::array<float, 8> accum;

  // Validate that +20.0 dB scales values by 10x. We calculate our own gain value rather than use
  // media_audio::ScaleToDb, as Mixer+Gain interactions (via APIs like that) are what we're testing.
  float desired_scale_factor = 10.0f;
  float stream_gain_db = DbFromScale(desired_scale_factor);  // 20.0f;
  auto mixer = SelectPointSampler(1, 1, 44100, 44100, fuchsia::media::AudioSampleFormat::SIGNED_16);
  DoMix(mixer.get(), source.data(), accum.data(), false, accum.size(), stream_gain_db);

  auto expect = std::vector<float>(8);
  for (auto idx = 0u; idx < expect.size(); ++idx) {
    expect[idx] = desired_scale_factor * static_cast<float>(source[idx]);
  }
  ShiftRightBy(expect, 15);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));

  // How precisely linear is a gain stage?  -12.0411998dB should cause 0.25x in value. Again, we
  // directly calculate a db value, since Gain APIs are within the scope that is being tested.
  desired_scale_factor = 0.25;
  stream_gain_db = DbFromScale(desired_scale_factor);  //-12.0411998f;
  mixer = SelectPointSampler(1, 1, 44100, 44100, fuchsia::media::AudioSampleFormat::SIGNED_16);

  DoMix(mixer.get(), source.data(), accum.data(), false, accum.size(), stream_gain_db);

  for (auto idx = 0u; idx < expect.size(); ++idx) {
    expect[idx] = desired_scale_factor * static_cast<float>(source[idx]);
  }
  ShiftRightBy(expect, 15);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// kMinGainDbUnity is the lowest gain_db with no observable attenuation of a full-scale signal
// (i.e. how far away from Unity can we be, and still be indistinguishable from Unity).
static constexpr float kMinGainDbUnity = -0.000000258856886667820f;
// This is the highest gain_db with an observable effect on a full-scale signal (i.e. the closest
// possible value to Unity that produces a different result).
static constexpr float kMaxGainDbNonUnity = -0.000000258865572365570f;
// Calculated as follows (validated on various devices/calculators/spreadsheets/etc.)
// Ratio (2^25-1)/2^25, multiplied by full-scale (1.0) float, produces hex equivalent 0x0.FFFFFF8
// Float lacks precision for the final "8" so the result will be rounded. Above this ratio, we are
// indistinguishable from Unity. At less than this ratio -- at least for full-scale signals -- we
// differ from Unity. MinGainUnity and MaxGainNonUnity are db values on EITHER side of this ratio.

// kMinGainDbNonMute is the lowest (closest-to-zero) gain_db at which audio is not silenced (i.e.
// the smallest gain distinguishable from Mute). Although results may be less than our "hex integer,
// right-shifted" pattern can represent, results are still non-zero and thus verify our scale limit.
static constexpr float kMinGainDbNonMute = -159.999992f;
// kMaxGainDbMute is the highest (furthest-from-Mute) gain that silences full scale data (i.e. the
// largest value INdistinguishable from Mute). Consider a gain_db ever-so-slightly above -160dB:
// if the increment is small enough, float32 treats it as -160dB, our "automatically mute" limit.
static constexpr float kMaxGainDbMute = -159.999993f;
// What db value is "half a float32 bit" less than 160.0? This "rounding boundary" marks where
// values become indistinguishable from 160.0 db itself.
// 160 in float is [mantissa: 1.25, binary exponent: 7]. Mantissa 1.25 is 0x1.400000 where the last
// hex digit has 3 significant bits. So "half a float32 bit" here is that final digit's least
// significant bit. Thus for float32, the dividing line between what IS and IS NOT distinguishable
// from -160.0f has a mantissa in hex of -0x1.3FFFFF.
// Reduced to formula, kMinGainDbNonMute|kMaxGainDbMute should be just greater|less than this value:
//
//   -1    *    (2^24 + (2^22 - 1)) / 2^24    *    2^7
//  sign        \------- mantissa -------/       exponent

// How does our gain scaling respond to scale values close to the limits?
// Using 16-bit inputs, verify the behavior of our Gain object when given the
// closest-to-Unity and closest-to-Mute scale values.
TEST_F(PointSamplerScalingTest, Precision) {
  auto max_source = std::array<int16_t, 2>{0x7FFF, -0x8000};  // max/min 16-bit signed values.
  auto accum = std::vector<float>(2);

  auto mixer = SelectPointSampler(1, 1, 48000, 48000, fuchsia::media::AudioSampleFormat::SIGNED_16);
  DoMix(mixer.get(), max_source.data(), accum.data(), false, accum.size(), kMinGainDbUnity);

  //  At this gain_scale, resulting audio should be unchanged.
  auto max_expect1 = std::vector<float>{0x7FFF, -0x8000};
  ShiftRightBy(max_expect1, 15);
  EXPECT_THAT(accum, Pointwise(FloatEq(), max_expect1));

  // mixer = SelectPointSampler(1, 1, 48000, 48000, fuchsia::media::AudioSampleFormat::SIGNED_16);
  DoMix(mixer.get(), max_source.data(), accum.data(), false, accum.size(), kMaxGainDbNonUnity);

  // Float32 has 25-bit precision (not 28), hence our min delta is 0x8 (not 1).
  auto max_expect2 = std::vector<float>{0x07FFEFF8, -0x07FFFFF8};
  ShiftRightBy(max_expect2, 27);
  EXPECT_THAT(accum, Pointwise(FloatEq(), max_expect2));

  auto min_source = std::array<int16_t, 2>{1, -1};
  // mixer = SelectPointSampler(1, 1, 48000, 48000, fuchsia::media::AudioSampleFormat::SIGNED_16);
  DoMix(mixer.get(), min_source.data(), accum.data(), false, accum.size(), kMinGainDbNonMute);

  // How we specify expectations for other tests (specify as integral float, shift-right) cannot
  // precisely express these values. Nonetheless, they are present and non-zero!
  auto min_expect = std::array<float, 2>{3.051763215e-13f, -3.051763215e-13f};
  EXPECT_THAT(accum, Pointwise(FloatEq(), min_expect));

  // Per mixer optimization, we skip mixing if gain is Mute-equivalent. This
  // is equivalent to setting 'accumulate' and adding zeroes, so set that flag here and expect no
  // change in the accumulator, even with max inputs.
  // mixer = SelectPointSampler(1, 1, 48000, 48000, fuchsia::media::AudioSampleFormat::SIGNED_16);
  DoMix(mixer.get(), max_source.data(), accum.data(), true, accum.size(), kMaxGainDbMute);

  EXPECT_THAT(accum, Pointwise(FloatEq(), min_expect));
}

//
// Timing (Resampling) tests
//
// Sync/timing correctness, to the sample level
// Verify correct FROM and TO locations, and quantity.
//
// Each test contains cases that exercise different code paths within the
// samplers.  A mix job's length is limited by the quantities of source data and
// output needed -- whichever is smaller. For this reason, we explicitly note
// places where we check "supply > demand", vs. "demand > supply", vs. "supply
// == demand". We used the PointSampler in earlier tests, so we already know
// "Supply == Demand" works there. When setting up each case, the so-called
// "supply" is determined by source_frames, and source_offset (into those frames).
// Likewise "demand" is determined by dest_frames and dest_offset into
// dest_frames.

// Verify that the samplers mix to/from correct buffer locations. Also ensure
// that they don't touch other buffer sections, regardless of 'accumulate'.
// This first test uses integer lengths/offsets, and a step_size of ONE.
class PointSamplerPositionTest : public PointSamplerTest {};

// Check: source supply equals destination demand.
TEST_F(PointSamplerPositionTest, BasicEqualSourceDest) {
  auto mixer = SelectPointSampler(1, 1, 48000, 48000, fuchsia::media::AudioSampleFormat::SIGNED_16);
  ASSERT_NE(mixer, nullptr);

  std::array<int16_t, 5> source{-0x00AA, 0x00BB, -0x00CC, 0x00DD, -0x00EE};
  int64_t source_frames = source.size();
  auto source_offset = Fixed(2);

  int64_t dest_frames = 4;
  int64_t dest_offset = 1;

  // Source (offset 2 of 5) has 3. Destination (offset 1 of 4) wants 3.
  // Mix will sum source[2,3,4] to accum[1,2,3]
  std::vector<float> accum{0x1100, -0x2200, 0x3300, -0x4400, 0x5500};
  std::vector<float> expect{0x1100, -0x22CC, 0x33DD, -0x44EE, 0x5500};
  ShiftRightBy(accum, 15);
  ShiftRightBy(expect, 15);

  mixer->Mix(accum.data(), dest_frames, &dest_offset, source.data(), source_frames, &source_offset,
             true);

  EXPECT_EQ(dest_offset, dest_frames);
  EXPECT_EQ(source_offset, Fixed(source_frames)) << std::hex << source_offset.raw_value();
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Check: source supply exceeds destination demand.
TEST_F(PointSamplerPositionTest, BasicSourceExceedsDemand) {
  auto mixer = SelectPointSampler(1, 1, 48000, 48000, fuchsia::media::AudioSampleFormat::SIGNED_16);
  ASSERT_NE(mixer, nullptr);

  std::array<int16_t, 5> source{-0x00AA, 0x00BB, -0x00CC, 0x00DD, -0x00EE};
  int64_t source_frames = source.size();
  auto source_offset = Fixed(0);

  int64_t dest_frames = 3;
  int64_t dest_offset = 1;

  // Source (offset 0 of 5) has 5. Destination (offset 1 of 3) wants 2.
  // Mix will sum source[0,1] to accum[1,2]
  std::vector<float> accum{0x1100, -0x2200, 0x3300, -0x4400, 0x5500};
  std::vector<float> expect{0x1100, -0x22AA, 0x33BB, -0x4400, 0x5500};
  ShiftRightBy(accum, 15);
  ShiftRightBy(expect, 15);

  mixer->Mix(accum.data(), dest_frames, &dest_offset, source.data(), source_frames, &source_offset,
             true);

  EXPECT_EQ(dest_offset, dest_frames);
  EXPECT_EQ(source_offset, Fixed(2)) << std::hex << source_offset.raw_value();
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Check: destination demand exceeds source supply.
TEST_F(PointSamplerPositionTest, BasicDestExceedsSource) {
  auto mixer = SelectPointSampler(1, 1, 48000, 48000, fuchsia::media::AudioSampleFormat::SIGNED_16);
  ASSERT_NE(mixer, nullptr);

  std::array<int16_t, 5> source{-0x00AA, 0x00BB, -0x00CC, 0x00DD, -0x00EE};
  int64_t source_frames = 4;
  auto source_offset = Fixed(3);

  int64_t dest_frames = 5;
  int64_t dest_offset = 0;

  // Source (offset 3 of 4) has 1. Destination (offset 0 of 5) wants 5.
  // Mix will sum source[3] to accum[0]
  std::vector<float> accum{0x1100, -0x2200, 0x3300, -0x4400, 0x5500};
  std::vector<float> expect{0x11DD, -0x2200, 0x3300, -0x4400, 0x5500};
  ShiftRightBy(accum, 15);
  ShiftRightBy(expect, 15);

  mixer->Mix(accum.data(), dest_frames, &dest_offset, source.data(), source_frames, &source_offset,
             true);

  EXPECT_EQ(dest_offset, 1);
  EXPECT_EQ(source_offset, Fixed(source_frames)) << std::hex << source_offset.raw_value();
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Validate basic (frame-level) position for SampleAndHold resampler.

// For PointSampler, test sample placement when given fractional position.
// Ensure it doesn't touch other buffer sections, regardless of 'accumulate'
// flag. Check when supply > demand and vice versa (we already know = works).
// These tests use fractional lengths/offsets, still with a step_size of ONE.
//
// Check: after factoring-in positive filter width, source position is exactly at a frame boundary.
TEST_F(PointSamplerPositionTest, FractionalPositionAtFrameBoundary) {
  auto mixer = SelectPointSampler(1, 1, 44100, 44100, fuchsia::media::AudioSampleFormat::SIGNED_16);

  ASSERT_NE(mixer, nullptr);

  // To accommodate "sample-and-hold" or "nearest-neighbor" implementations without changing this
  // test, we expressly factor-in positive width. Our starting position is in the range (1.0, 2.0],
  // where this guarantees that Source has 3. Destination (offset 1 of 3) wants 2.
  Fixed source_offset = Fixed(2) - mixer->pos_filter_width();
  Fixed expect_source_offset = source_offset + Fixed(2);
  std::array<int16_t, 5> source{-0x00AA, 0x00BB, -0x00CC, 0x00DD, -0x00EE};
  int64_t source_frames = source.size();

  int64_t dest_frames = 3;
  int64_t dest_offset = 1;
  // We set position so that for fractional source[1:2, 2:3], PointSampler will choose source[2,3].
  // Thus Mix will sum source[2,3] into accum[1,2].
  std::vector<float> accum{0x1100, -0x2200, 0x3300, -0x4400, 0x5500};
  std::vector<float> expect{0x1100, -0x22CC, 0x33DD, -0x4400, 0x5500};
  ShiftRightBy(accum, 15);
  ShiftRightBy(expect, 15);

  mixer->Mix(accum.data(), dest_frames, &dest_offset, source.data(), source_frames, &source_offset,
             true);

  EXPECT_EQ(dest_offset, dest_frames);
  EXPECT_EQ(source_offset, expect_source_offset) << std::hex << source_offset.raw_value();
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Check: factoring-in positive filter width, source position is just short of a frame boundary.
TEST_F(PointSamplerPositionTest, FractionalPositionJustBeforeFrameBoundary) {
  auto mixer = SelectPointSampler(1, 1, 44100, 44100, fuchsia::media::AudioSampleFormat::SIGNED_16);

  ASSERT_NE(mixer, nullptr);

  // To accommodate "sample-and-hold" or "nearest-neighbor" implementations without changing this
  // test, we expressly factor-in positive width. Our starting position is in the range [1.0, 2.0),
  // where this guarantees that Source has 4. Destination (offset 2 of 4) wants 2.
  Fixed source_offset = Fixed(2) - mixer->pos_filter_width() - Fixed::FromRaw(1);
  Fixed expect_source_offset = source_offset + Fixed(2);
  std::array<int16_t, 5> source{-0x00AA, 0x00BB, -0x00CC, 0x00DD, -0x00EE};
  int64_t source_frames = source.size();

  int64_t dest_frames = 4;
  int64_t dest_offset = 2;
  // We set position so that for fractional source[1:2, 2:3], PointSampler will choose source[1,2].
  // Thus Mix will sum source[1,2] into accum[2,3].
  std::vector<float> accum{0x1100, -0x2200, 0x3300, -0x4400, 0x5500};
  std::vector<float> expect{0x1100, -0x2200, 0x33BB, -0x44CC, 0x5500};
  ShiftRightBy(accum, 15);
  ShiftRightBy(expect, 15);

  mixer->Mix(accum.data(), dest_frames, &dest_offset, source.data(), source_frames, &source_offset,
             true);

  EXPECT_EQ(dest_offset, dest_frames);
  EXPECT_EQ(source_offset, expect_source_offset) << std::hex << source_offset.raw_value();
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// When setting the frac_source_pos to a value that is at the end (or within pos_filter_width) of
// the source buffer, the sampler should not mix additional frames (neither dest_offset nor
// source_offset should be advanced).
TEST_F(PointSamplerPositionTest, SourceOffsetAtEnd) {
  auto mixer = SelectPointSampler(1, 1, 44100, 44100, fuchsia::media::AudioSampleFormat::FLOAT);
  ASSERT_NE(mixer, nullptr);

  std::array<float, 4> source{1.0f, 1.0f, 1.0f, 1.0f};
  Fixed source_offset = Fixed(std::size(source)) - mixer->pos_filter_width();
  const auto initial_source_offset = source_offset;

  std::array<float, 4> accum{0.0f};
  int64_t dest_offset = 0;

  mixer->state().ResetSourceStride(TimelineRate(Fixed(1).raw_value(), 1));
  mixer->Mix(accum.data(), accum.size(), &dest_offset, source.data(), source.size(), &source_offset,
             false);

  EXPECT_EQ(dest_offset, 0);
  EXPECT_EQ(source_offset, initial_source_offset);
  EXPECT_EQ(accum[0], 0.0f);
}

// Verify PointSampler filter width. Current implementation is "FORWARD nearest neighbor".
// In other words, when exactly midway between two source frames, we sample the NEWER one.
TEST_F(PointSamplerPositionTest, FilterWidth) {
  int64_t expect_pos_width = kHalfFrame.raw_value();
  int64_t expect_neg_width = kHalfFrame.raw_value() - 1;

  auto mixer =
      SelectPointSampler(1, 1, 48000, 48000, fuchsia::media::AudioSampleFormat::UNSIGNED_8);

  EXPECT_EQ(mixer->pos_filter_width().raw_value(), expect_pos_width);
  EXPECT_EQ(mixer->neg_filter_width().raw_value(), expect_neg_width);
}

}  // namespace
}  // namespace media::audio::mixer

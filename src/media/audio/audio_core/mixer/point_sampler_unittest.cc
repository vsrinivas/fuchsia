// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/point_sampler.h"

#include <fbl/algorithm.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace media::audio::mixer {
namespace {

using testing::Each;
using testing::Eq;
using testing::FloatEq;
using testing::Pointwise;

using Resampler = ::media::audio::Mixer::Resampler;

class PointSamplerTest : public testing::Test {
 protected:
  static const std::vector<uint32_t> kFrameRates;
  static const std::vector<uint32_t> kUnsupportedFrameRates;

  static const std::vector<std::pair<uint32_t, uint32_t>> kChannelConfigs;
  static const std::vector<std::pair<uint32_t, uint32_t>> kUnsupportedChannelConfigs;

  static const std::vector<fuchsia::media::AudioSampleFormat> kFormats;
  const fuchsia::media::AudioSampleFormat kInvalidFormat =
      static_cast<fuchsia::media::AudioSampleFormat>(
          static_cast<uint64_t>(kFormats[std::size(kFormats) - 1]) + 1);

  std::unique_ptr<Mixer> SelectPointSampler(
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
  void DoMix(Mixer* mixer, const void* source_buf, float* accum_buf, bool accumulate,
             int32_t num_frames) {
    ASSERT_NE(mixer, nullptr);

    uint32_t dest_offset = 0;
    int32_t frac_source_offset = 0;

    auto& info = mixer->bookkeeping();
    info.gain.SetSourceGain(Gain::kUnityGainDb);

    bool mix_result = mixer->Mix(accum_buf, num_frames, &dest_offset, source_buf,
                                 num_frames << kPtsFractionalBits, &frac_source_offset, accumulate);

    EXPECT_TRUE(mix_result);
    EXPECT_EQ(dest_offset, static_cast<uint32_t>(num_frames));
    EXPECT_EQ(frac_source_offset, static_cast<int32_t>(dest_offset << kPtsFractionalBits));
  }
};

const std::vector<uint32_t> PointSamplerTest::kFrameRates = {
    8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000, 176400, 192000,
};

const std::vector<uint32_t> PointSamplerTest::kUnsupportedFrameRates = {
    fuchsia::media::MIN_PCM_FRAMES_PER_SECOND - 1,
    fuchsia::media::MAX_PCM_FRAMES_PER_SECOND + 1,
};

const std::vector<std::pair<uint32_t, uint32_t>> PointSamplerTest::kChannelConfigs = {
    {1, 1}, {1, 2}, {1, 3}, {1, 4},  //
    {2, 1}, {2, 2}, {2, 3}, {2, 4},  //
    {3, 1}, {3, 2}, {3, 3},          //
    {4, 1}, {4, 2}, {4, 4},          //
    {5, 5}, {6, 6}, {7, 7}, {8, 8},
};

const std::vector<std::pair<uint32_t, uint32_t>> PointSamplerTest::kUnsupportedChannelConfigs = {
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
                                        << rate << ", format " << static_cast<uint64_t>(format));
        EXPECT_NE(mixer, nullptr);
      }
    }
  }
}

// Rate mismatch is unsupported
TEST_F(PointSamplerTest, Construction_DifferingRates) {
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
                                      << dest_rate << ", format " << static_cast<uint64_t>(format));
      EXPECT_EQ(nullptr, SelectPointSampler(channel_config.first, channel_config.second,
                                            source_rate, dest_rate, format));

      channel_config = kChannelConfigs[std::size(kChannelConfigs) - 1];
      format = kFormats[std::size(kFormats) - 1];

      SCOPED_TRACE(testing::Message() << "Chans " << channel_config.first << ">"
                                      << channel_config.second << ", rates " << source_rate << ":"
                                      << dest_rate << ", format " << static_cast<uint64_t>(format));
      EXPECT_EQ(nullptr, SelectPointSampler(channel_config.first, channel_config.second,
                                            source_rate, dest_rate, format));
    }
  }
}

// Out-of-range rates are unsupported
TEST_F(PointSamplerTest, Construction_UnsupportedRate) {
  for (auto bad_rate : kUnsupportedFrameRates) {
    // Use channel configs and formats that are known-good.
    auto channel_config = kChannelConfigs[0];
    auto format = kFormats[0];

    SCOPED_TRACE(testing::Message()
                 << "Chans " << channel_config.first << ">" << channel_config.second << ", rates "
                 << bad_rate << ":" << bad_rate << ", format " << static_cast<uint64_t>(format));
    EXPECT_EQ(nullptr, SelectPointSampler(channel_config.first, channel_config.second, bad_rate,
                                          bad_rate, format));

    channel_config = kChannelConfigs[std::size(kChannelConfigs) - 1];
    format = kFormats[std::size(kFormats) - 1];

    SCOPED_TRACE(testing::Message()
                 << "Chans " << channel_config.first << ">" << channel_config.second << ", rates "
                 << bad_rate << ":" << bad_rate << ", format " << static_cast<uint64_t>(format));
    EXPECT_EQ(nullptr, SelectPointSampler(channel_config.first, channel_config.second, bad_rate,
                                          bad_rate, format));
  }
}

// These channel configs are unsupported
TEST_F(PointSamplerTest, Construction_UnsupportedChannelConfig) {
  for (auto bad_channel_config : kUnsupportedChannelConfigs) {
    // Use rates and formats that are known-good.
    auto rate = kFrameRates[0];
    auto format = fuchsia::media::AudioSampleFormat::SIGNED_16;

    SCOPED_TRACE(testing::Message() << "Chans " << bad_channel_config.first << ">"
                                    << bad_channel_config.second << ", rates " << rate << ":"
                                    << rate << ", format " << static_cast<uint64_t>(format));
    EXPECT_EQ(nullptr, SelectPointSampler(bad_channel_config.first, bad_channel_config.second, rate,
                                          rate, format));

    rate = kFrameRates[std::size(kFrameRates) - 1];
    format = fuchsia::media::AudioSampleFormat::FLOAT;

    SCOPED_TRACE(testing::Message() << "Chans " << bad_channel_config.first << ">"
                                    << bad_channel_config.second << ", rates " << rate << ":"
                                    << rate << ", format " << static_cast<uint64_t>(format));
    EXPECT_EQ(nullptr, SelectPointSampler(bad_channel_config.first, bad_channel_config.second, rate,
                                          rate, format));
  }
}

// This format is unsupported
TEST_F(PointSamplerTest, Construction_UnsupportedFormat) {
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

// Can 8-bit values flow unchanged (1-1, N-N) thru the system? With 1:1 frame
// conversion, unity scale and no accumulation, we expect bit-equality.
TEST_F(PointSamplerTest, PassThru_8) {
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
TEST_F(PointSamplerTest, PassThru_16) {
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
TEST_F(PointSamplerTest, PassThru_24) {
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
TEST_F(PointSamplerTest, PassThru_Float) {
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

// Are all valid data values passed correctly to 16-bit outputs for the 1->2 channel mapping.
TEST_F(PointSamplerTest, PassThru_MonoToStereo) {
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
TEST_F(PointSamplerTest, PassThru_StereoToMono) {
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
TEST_F(PointSamplerTest, PassThru_QuadToMono) {
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
  auto expect = std::vector<float>{
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
  ShiftRightBy(expect, 23);  // right-shift these "int24" values into float range

  auto mixer =
      SelectPointSampler(4, 1, 64000, 64000, fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32);
  DoMix(mixer.get(), source.data(), accum.data(), false, accum.size());
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Validate quad->stereo mixing, including sub-format precision. Note: 0|1|2|3 becomes 0+2 | 1+3
TEST_F(PointSamplerTest, PassThru_QuadToStereo) {
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
  auto expect = std::vector<float>{
      // clang-format off
                0.5,      -0.5,
        -0x800000,  0x7FFFFF,
                0,         0,
      // clang-format on
  };
  ShiftRightBy(expect, 23);  // right-shift these "int24" values into float range

  auto mixer =
      SelectPointSampler(4, 2, 22050, 22050, fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32);
  DoMix(mixer.get(), source.data(), accum.data(), false,
        accum.size() / 2);  // dest frames have 2 samples each
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Are all valid data values passed correctly to 16-bit outputs for the 1->4 channel mapping?
TEST_F(PointSamplerTest, PassThru_MonoToQuad) {
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
TEST_F(PointSamplerTest, PassThru_StereoToQuad) {
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
    expect[idx * 2] = source[idx];          // First sample of each output frame should be L
    expect[idx * 2 + 1] = source[idx + 1];  // Second sample of each output frame should be R
    expect[idx * 2 + 2] = source[idx];      // Third  sample of each output frame should be L
    expect[idx * 2 + 3] = source[idx + 1];  // Fourth sample of each output frame should be R
  }
  ShiftRightBy(expect, 31);  // right-shift these int32 values into float range

  auto mixer =
      SelectPointSampler(2, 4, 48000, 48000, fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32);
  DoMix(mixer.get(), source.data(), accum.data(), false, accum.size() / 4);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Do we obey the 'accumulate' flag if mixing into existing accumulated data?
// Most of the above tests depend on accum FALSE working correctly: just validate TRUE here.
TEST_F(PointSamplerTest, PassThru_Accumulate) {
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

}  // namespace
}  // namespace media::audio::mixer

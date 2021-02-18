// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <iterator>

#include <fbl/algorithm.h>

#include "src/media/audio/audio_core/mixer/no_op.h"
#include "src/media/audio/audio_core/mixer/test/mixer_tests_shared.h"

using testing::Each;
using testing::Eq;
using testing::FloatEq;
using testing::Pointwise;

namespace media::audio::test {

// Convenience abbreviation within this source file to shorten names
using Resampler = ::media::audio::Mixer::Resampler;

// TODO(mpuryear): use value-parameterized tests for these and other test cases throughout this file

// Are all valid data values passed correctly to 16-bit outputs
TEST(PassThru, MonoToStereo) {
  int16_t source[] = {-0x8000, -0x3FFF, -1, 0, 1, 0x7FFF};
  float accum[6 * 2];

  float expect[] = {-0x08000000, -0x08000000, -0x03FFF000, -0x03FFF000, -0x0001000, -0x00001000,
                    0,           0,           0x0001000,   0x00001000,  0x07FFF000, 0x07FFF000};
  NormalizeInt28ToPipelineBitwidth(expect, std::size(expect));

  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 2, 48000,
                           Resampler::SampleAndHold);

  DoMix(mixer.get(), source, accum, false, std::size(accum) / 2);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Do we correctly mix stereo to mono, when channels sum to exactly zero
TEST(PassThru, StereoToMono_Cancel) {
  int16_t source[] = {32767, -32767, -23130, 23130, 0, 0, 1, -1, -13107, 13107, 3855, -3855};
  float accum[6];

  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 2, 48000, 1, 48000,
                           Resampler::SampleAndHold);

  DoMix(mixer.get(), source, accum, false, std::size(accum));
  EXPECT_THAT(accum, Each(FloatEq(0.0f)));
}

// Validate that we correctly mix stereo->mono, including rounding.
TEST(PassThru, StereoToMono_Round) {
  // pairs: positive even, neg even, pos odd, neg odd, pos limit, neg limit
  int16_t source[] = {-0x13,   0x2EF5,  0x7B,   -0x159, -0x3E8,  0x3ED,
                      -0x103B, -0x1B58, 0x7FFF, 0x7FFF, -0x8000, -0x8000};
  // Will be overwritten
  float accum[] = {-0x1234, 0x4321, -0x13579, 0xC0FF, -0xAAAA, 0x555};

  float expect[] = {0x01771000, -0x0006F000, 0x00002800, -0x015C9800, 0x07FFF000, -0x08000000};
  NormalizeInt28ToPipelineBitwidth(expect, std::size(expect));

  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 2, 48000, 1, 48000,
                           Resampler::SampleAndHold);

  DoMix(mixer.get(), source, accum, false, std::size(accum));
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Validate that we correctly mix quad->mono, including rounding.
TEST(PassThru, QuadToMono) {
  // combinations: positive even, neg even, pos odd, neg odd, pos limit, neg limit, zero
  int32_t source[] = {                  // clang-format off
     0x100,         0,              0,              0,               // should become 0.25
    -0x100,         0,              0,              0,               // should become -0.25
     kMinInt24In32, kMinInt24In32,  kMinInt24In32,  kMinInt24In32,   // should become kMinInt32In32
     kMaxInt24In32, kMaxInt24In32,  kMaxInt24In32,  kMaxInt24In32,   // should become kMaxInt24In32
     kMaxInt24In32, kMaxInt24In32, -kMaxInt24In32, -kMaxInt24In32};  // should become 0
                                        // clang-format on
  // Will be overwritten
  float accum[] = {-0x1234, 0x4321, -0x13579, 0xC0FF, -0xAAAA};
  static_assert(std::size(source) == std::size(accum) * 4, "buf sizes must match");

  // Equivalent to 0.25, -0.25, min val (largest neg), max val, 0
  float expect[] = {0x0000004, -0x0000004, (kMinInt24In32 >> 4), (kMaxInt24In32) >> 4, 0};
  static_assert(std::size(accum) == std::size(expect), "buf sizes must match");
  NormalizeInt28ToPipelineBitwidth(expect, std::size(expect));

  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 4, 24000, 1, 24000,
                           Resampler::SampleAndHold);

  DoMix(mixer.get(), source, accum, false, std::size(accum));
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Validate that we correctly mix quad->stereo, including rounding. Note: 0|1|2|3 becomes 0+2 | 1+3
TEST(PassThru, QuadToStereo_Round) {
  // combinations: positive even, neg even, pos odd, neg odd, pos limit, neg limit, zero
  int32_t source[] = {// clang-format off
                       0x100,        -0x100,
                       0,             0,
                       kMinInt24In32, kMaxInt24In32,
                       kMinInt24In32, kMaxInt24In32,
                       kMaxInt24In32, 0,
                      -kMaxInt24In32, 0 };
                      // clang-format on

  // Will be overwritten
  float accum[] = {-0x1234, 0x4321, -0x13579, 0xC0FF, -0xAAAA, 0x555};
  static_assert(std::size(source) == std::size(accum) * 2, "buf sizes must match");

  // Equivalent to 0.5, -0.5, min val (largest neg), max val, 0
  float expect[] = {0x0000008, -0x0000008, (kMinInt24In32 >> 4), (kMaxInt24In32) >> 4, 0, 0};
  static_assert(std::size(accum) == std::size(expect), "buf sizes must match");
  NormalizeInt28ToPipelineBitwidth(expect, std::size(expect));

  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 4, 22050, 2, 22050,
                           Resampler::SampleAndHold);

  DoMix(mixer.get(), source, accum, false, std::size(accum) / 2);  // dest frames have 2 samples
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Are all valid data values passed correctly to 16-bit outputs
TEST(PassThru, MonoToQuad) {
  int16_t source[] = {-0x8000, -0x3FFF, -1, 0, 1, 0x7FFF};
  float accum[6 * 4];
  float expect[] = {-0x08000000, -0x08000000, -0x08000000, -0x08000000, -0x03FFF000, -0x03FFF000,
                    -0x03FFF000, -0x03FFF000, -0x0001000,  -0x00001000, -0x0001000,  -0x00001000,
                    0,           0,           0,           0,           0x0001000,   0x00001000,
                    0x0001000,   0x00001000,  0x07FFF000,  0x07FFF000,  0x07FFF000,  0x07FFF000};

  static_assert(std::size(source) * 4 == std::size(accum), "buf sizes must match");
  static_assert(std::size(accum) == std::size(expect), "buf sizes must match");
  NormalizeInt28ToPipelineBitwidth(expect, std::size(expect));

  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1, 48000, 4, 48000,
                           Resampler::SampleAndHold);

  DoMix(mixer.get(), source, accum, false, std::size(accum) / 4);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Are all valid data values passed correctly to 16-bit outputs
TEST(PassThru, StereoToQuad) {
  int32_t source[] = {kMinInt24In32, -0x3FFFFF00, -0x100, 0, 0x100, kMaxInt24In32};
  float accum[3 * 4];
  float expect[] = {-0x08000000, -0x03FFFFF0, -0x08000000, -0x03FFFFF0, -0x0000010, 0,
                    -0x00000010, 0,           0x0000010,   0x07FFFFF0,  0x00000010, 0x07FFFFF0};

  static_assert((std::size(source) / 2) * 4 == std::size(accum), "buf sizes must match");
  static_assert(std::size(accum) == std::size(expect), "buf sizes must match");
  NormalizeInt28ToPipelineBitwidth(expect, std::size(expect));

  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 2, 48000, 4, 48000,
                           Resampler::SampleAndHold);

  DoMix(mixer.get(), source, accum, false, std::size(accum) / 4);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));
}

// Do we obey the 'accumulate' flag if mixing into existing accumulated data?
TEST(PassThru, Accumulate) {
  int16_t source[] = {-0x10E1, 0x0929, 0x1A85, -0x223D};

  float accum[] = {0x056CE240, 0x02B67930, -0x015B2000, 0x0259EB00};
  float expect[] = {0x045ED240, 0x03490930, 0x004D3000, 0x00361B00};
  NormalizeInt28ToPipelineBitwidth(accum, std::size(accum));
  NormalizeInt28ToPipelineBitwidth(expect, std::size(expect));

  auto mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 2, 48000, 2, 48000,
                           Resampler::SampleAndHold);

  DoMix(mixer.get(), source, accum, true, std::size(accum) / 2);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect));

  float expect2[] = {-0x010E1000, 0x00929000, 0x01A85000, -0x0223D000};  // =source
  NormalizeInt28ToPipelineBitwidth(expect2, std::size(expect2));
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 2, 48000, 2, 48000,
                      Resampler::SampleAndHold);

  DoMix(mixer.get(), source, accum, false, std::size(accum) / 2);
  EXPECT_THAT(accum, Pointwise(FloatEq(), expect2));
}

}  // namespace media::audio::test

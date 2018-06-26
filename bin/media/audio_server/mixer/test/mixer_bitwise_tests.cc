// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>
#include "garnet/bin/media/audio_server/mixer/no_op.h"
#include "garnet/bin/media/audio_server/mixer/test/mixer_tests_shared.h"
#include "lib/fxl/logging.h"

namespace media {
namespace audio {
namespace test {

// Convenience abbreviation within this source file to shorten names
using Resampler = ::media::audio::Mixer::Resampler;

//
// DataFormats tests - can we "connect the dots" from data source to data
// destination, for any permutation of format/configuration settings
//
// If the source sample rate is an integer-multiple of the destination rate
// (including 1, for pass-thru resampling), select the PointSampler
//
// Create PointSampler objects for incoming buffers of type uint8
TEST(DataFormats, PointSampler_8) {
  EXPECT_NE(nullptr, SelectMixer(fuchsia::media::AudioSampleFormat::UNSIGNED_8,
                                 2, 32000, 1, 16000, Resampler::SampleAndHold));
  EXPECT_NE(nullptr, SelectMixer(fuchsia::media::AudioSampleFormat::UNSIGNED_8,
                                 4, 48000, 4, 48000));
}

// Create PointSampler objects for incoming buffers of type int16
TEST(DataFormats, PointSampler_16) {
  EXPECT_NE(nullptr, SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16,
                                 1, 24000, 1, 24000, Resampler::SampleAndHold));
  EXPECT_NE(nullptr, SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16,
                                 1, 44100, 2, 11025, Resampler::Default));
}

// Create PointSampler objects for incoming buffers of type float
TEST(DataFormats, PointSampler_Float) {
  EXPECT_NE(nullptr, SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 2,
                                 48000, 2, 16000));
}

// Create PointSampler objects for other formats of incoming buffers
// This is not expected to work, as these are not yet implemented
TEST(DataFormats, PointSampler_Other) {
  EXPECT_EQ(nullptr,
            SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 2,
                        8000, 1, 8000, Resampler::SampleAndHold));
}

// If the source sample rate is NOT an integer-multiple of the destination rate
// (including when the destination is an integer multiple of the SOURCE rate),
// select the LinearSampler
//
// Create LinearSampler objects for incoming buffers of type uint8
TEST(DataFormats, LinearSampler_8) {
  EXPECT_NE(nullptr,
            SelectMixer(fuchsia::media::AudioSampleFormat::UNSIGNED_8, 1, 22050,
                        2, 44100, Resampler::LinearInterpolation));
  EXPECT_NE(nullptr, SelectMixer(fuchsia::media::AudioSampleFormat::UNSIGNED_8,
                                 2, 44100, 1, 48000));
}

// Create LinearSampler objects for incoming buffers of type int16
TEST(DataFormats, LinearSampler_16) {
  EXPECT_NE(nullptr,
            SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 2, 16000,
                        2, 48000, Resampler::LinearInterpolation));
  EXPECT_NE(nullptr, SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16,
                                 2, 44100, 1, 48000, Resampler::Default));
  EXPECT_NE(nullptr, SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16,
                                 8, 48000, 8, 44100));
}

// Create LinearSampler objects for incoming buffers of type float
TEST(DataFormats, LinearSampler_Float) {
  EXPECT_NE(nullptr, SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 2,
                                 48000, 2, 44100));
}

// Create LinearSampler objects for other formats of incoming buffers
// This is not expected to work, as these are not yet implemented
TEST(DataFormats, LinearSampler_Other) {
  EXPECT_EQ(nullptr,
            SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 2,
                        8000, 1, 11025));
}

// Create OutputFormatter objects for outgoing buffers of type uint8
TEST(DataFormats, OutputFormatter_8) {
  EXPECT_NE(nullptr, SelectOutputFormatter(
                         fuchsia::media::AudioSampleFormat::UNSIGNED_8, 2));
}

// Create OutputFormatter objects for outgoing buffers of type int16
TEST(DataFormats, OutputFormatter_16) {
  EXPECT_NE(nullptr, SelectOutputFormatter(
                         fuchsia::media::AudioSampleFormat::SIGNED_16, 4));
}

// Create OutputFormatter objects for outgoing buffers of type float
TEST(DataFormats, OutputFormatter_Float) {
  EXPECT_NE(nullptr,
            SelectOutputFormatter(fuchsia::media::AudioSampleFormat::FLOAT, 1));
}

// Create OutputFormatter objects for other output formats
// This is not expected to work, as these are not yet implemented
TEST(DataFormats, OutputFormatter_Other) {
  EXPECT_EQ(nullptr,
            SelectOutputFormatter(
                fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 3));
}

//
// PassThru tests - can audio data flow through the different stages in our
// system without being altered, using numerous possible configurations?
//
// When doing direct bit-for-bit comparisons in these tests, we must factor in
// the left-shift biasing that is done while converting input data into the
// internal format of our accumulator.  For this reason, all "expect" values are
// specified at a higher-than-needed precision of 24-bit, and then normalized
// down to the actual pipeline width.

// Can 8-bit values flow unchanged (1-1, N-N) thru the system? With 1:1 frame
// conversion, unity scale and no accumulation, we expect bit-equality.
TEST(PassThru, Source_8) {
  uint8_t source[] = {0x00, 0xFF, 0x27, 0xCD, 0x7F, 0x80, 0xA6, 0x6D};
  int32_t accum[8];

  int32_t expect[] = {-0x08000000, 0x07F00000, -0x05900000, 0x04D00000,
                      -0x00100000, 0,          0x02600000,  -0x01300000};
  NormalizeInt28ToPipelineBitwidth(expect, fbl::count_of(expect));

  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::UNSIGNED_8, 1,
                               48000, 1, 48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum));
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::UNSIGNED_8, 8, 48000,
                      8, 48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum) / 8);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));
}

// Can 16-bit values flow unchanged (2-2, N-N) thru the system? With 1:1 frame
// conversion, unity scale and no accumulation, we expect bit-equality.
TEST(PassThru, Source_16) {
  int16_t source[] = {-0x8000, 0x7FFF, -0x67A7, 0x4D4D,
                      -0x123,  0,      0x2600,  -0x2DCB};
  int32_t accum[8];

  int32_t expect[] = {-0x08000000, 0x07FFF000, -0x067A7000, 0x04D4D000,
                      -0x00123000, 0,          0x02600000,  -0x02DCB000};
  NormalizeInt28ToPipelineBitwidth(expect, fbl::count_of(expect));

  // Try in 2-channel mode
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 2,
                               48000, 2, 48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum) / 2);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  ::memset(accum, 0, sizeof(accum));
  // Now try in 4-channel mode
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 4, 48000, 4,
                      48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum) / 4);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));
}

// Can float values flow unchanged (1-1, N-N) thru the system? With 1:1 frame
// conversion, unity scale and no accumulation, we expect bit-equality.
TEST(PassThru, Source_Float) {
  float source[] = {
      -1.0, 1.0f,      -0.809783935f, 0.603912353f, -0.00888061523f,
      0.0f, 0.296875f, -0.357757568f};
  int32_t accum[8];

  int32_t expect[] = {-0x08000000, 0x08000000, -0x067A7000, 0x04D4D000,
                      -0x00123000, 0,          0x02600000,  -0x02DCB000};
  NormalizeInt28ToPipelineBitwidth(expect, fbl::count_of(expect));

  // Try in 1-channel mode
  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 1,
                               48000, 1, 48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum));
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  ::memset(accum, 0, sizeof(accum));
  // Now try in 4-channel mode
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::FLOAT, 4, 48000, 4,
                      48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum) / 4);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));
}

// Does NoOp mixer behave as expected? (not update offsets, nor touch buffers)
TEST(PassThru, NoOp) {
  MixerPtr no_op_mixer = MixerPtr(new mixer::NoOp());
  EXPECT_NE(nullptr, no_op_mixer);

  int16_t source[] = {0x7FFF, -0x8000};
  int32_t accum[] = {-1, 42};
  int32_t expect[] = {-1, 42};

  uint32_t dst_offset = 0;
  int32_t frac_src_offset = 0;

  bool mix_result = no_op_mixer->Mix(
      accum, fbl::count_of(accum), &dst_offset, source,
      fbl::count_of(source) << kPtsFractionalBits, &frac_src_offset,
      Mixer::FRAC_ONE, Gain::kUnityScale, false);

  EXPECT_FALSE(mix_result);
  EXPECT_EQ(0u, dst_offset);
  EXPECT_EQ(0, frac_src_offset);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));
}

// Are all valid data values passed correctly to 16-bit outputs
TEST(PassThru, MonoToStereo) {
  int16_t source[] = {-0x8000, -0x3FFF, -1, 0, 1, 0x7FFF};
  int32_t accum[6 * 2];

  int32_t expect[] = {-0x08000000, -0x08000000, -0x03FFF000, -0x03FFF000,
                      -0x0001000,  -0x00001000, 0,           0,
                      0x0001000,   0x00001000,  0x07FFF000,  0x07FFF000};
  NormalizeInt28ToPipelineBitwidth(expect, fbl::count_of(expect));

  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 1,
                               48000, 2, 48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum) / 2);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));
}

// Do we correctly mix stereo to mono, when channels sum to exactly zero
TEST(PassThru, StereoToMono_Cancel) {
  int16_t source[] = {32767, -32767, -23130, 23130, 0,    0,
                      1,     -1,     -13107, 13107, 3855, -3855};
  int32_t accum[6];

  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 2,
                               48000, 1, 48000, Resampler::SampleAndHold);

  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum));
  EXPECT_TRUE(CompareBufferToVal(accum, 0, fbl::count_of(accum)));
}

// Validate that we correctly mix stereo->mono, including rounding.
TEST(PassThru, StereoToMono_Round) {
  // pairs: positive even, neg even, pos odd, neg odd, pos limit, neg limit
  int16_t source[] = {-0x13,   0x2EF5,  0x7B,   -0x159, -0x3E8,  0x3ED,
                      -0x103B, -0x1B58, 0x7FFF, 0x7FFF, -0x8000, -0x8000};
  // Will be overwritten
  int32_t accum[] = {-0x1234, 0x4321, -0x13579, 0xC0FF, -0xAAAA, 0x555};

  int32_t expect[] = {0x01771000,  -0x0006F000, 0x00002800,
                      -0x015C9800, 0x07FFF000,  -0x08000000};
  NormalizeInt28ToPipelineBitwidth(expect, fbl::count_of(expect));

  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 2,
                               48000, 1, 48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum));
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));
}

// Do we obey the 'accumulate' flag if mixing into existing accumulated data?
TEST(PassThru, Accumulate) {
  int16_t source[] = {-0x10E1, 0x0929, 0x1A85, -0x223D};

  int32_t accum[] = {0x056CE240, 0x02B67930, -0x015B2000, 0x0259EB00};
  int32_t expect[] = {0x045ED240, 0x03490930, 0x004D3000, 0x00361B00};
  NormalizeInt28ToPipelineBitwidth(accum, fbl::count_of(accum));
  NormalizeInt28ToPipelineBitwidth(expect, fbl::count_of(expect));

  MixerPtr mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 2,
                               48000, 2, 48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, true, fbl::count_of(accum) / 2);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  int32_t expect2[] = {-0x010E1000, 0x00929000, 0x01A85000,
                       -0x0223D000};  // =source
  NormalizeInt28ToPipelineBitwidth(expect2, fbl::count_of(expect2));
  mixer = SelectMixer(fuchsia::media::AudioSampleFormat::SIGNED_16, 2, 48000, 2,
                      48000, Resampler::SampleAndHold);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum) / 2);
  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));
}

// Are all valid data values rounded correctly to 8-bit outputs?
TEST(PassThru, Output_8) {
  int32_t accum[] = {-0x08080000, -0x08000000, -0x04080000, -0x00001000,
                     //   ^^^^^  clamp to uint8   vvvvv
                     0, 0x04080000, 0x07FFF000, 0x08000000};
  NormalizeInt28ToPipelineBitwidth(accum, fbl::count_of(accum));

  // Dest completely overwritten, except for last value: we only mix(8)
  uint8_t dest[] = {12, 23, 34, 45, 56, 67, 78, 89, 42};
  uint8_t expect[] = {0x0, 0x0, 0x3F, 0x80, 0x80, 0xC1, 0xFF, 0xFF, 42};

  OutputFormatterPtr output_formatter =
      SelectOutputFormatter(fuchsia::media::AudioSampleFormat::UNSIGNED_8, 1);

  output_formatter->ProduceOutput(accum, reinterpret_cast<void*>(dest),
                                  fbl::count_of(accum));
  EXPECT_TRUE(CompareBuffers(dest, expect, fbl::count_of(dest)));
}

// Are all valid data values passed correctly to 16-bit outputs?
TEST(PassThru, Output_16) {
  int32_t accum[] = {-0x08080000, -0x08000000, -0x04080000, -0x00001000,
                     //   ^^^^^   clamp to int16   vvvvv
                     0, 0x04080000, 0x07FFF000, 0x08000000};
  NormalizeInt28ToPipelineBitwidth(accum, fbl::count_of(accum));

  // Dest buffer is overwritten, EXCEPT for last value: we only mix(8)
  int16_t dest[] = {0123, 1234, 2345, 3456, 4567, 5678, 6789, 7890, -42};
  int16_t expect[] = {-0x8000, -0x8000, -0x4080, -1, 0,
                      0x4080,  0x7FFF,  0x7FFF,  -42};

  OutputFormatterPtr output_formatter =
      SelectOutputFormatter(fuchsia::media::AudioSampleFormat::SIGNED_16, 2);

  output_formatter->ProduceOutput(accum, reinterpret_cast<void*>(dest),
                                  fbl::count_of(accum) / 2);
  EXPECT_TRUE(CompareBuffers(dest, expect, fbl::count_of(dest)));
}

// Are all valid data values passed correctly to float outputs
TEST(PassThru, Output_Float) {
  int32_t accum[] = {-0x08080000, -0x08000000, -0x04080000, -0x00001000,
                     //   ^^^^ clamp to [-1.0,1.0] vvvv
                     0, 0x04080000, 0x07FFF000, 0x08080000};
  NormalizeInt28ToPipelineBitwidth(accum, fbl::count_of(accum));

  float dest[] = {1.2f, 2.3f, 3.4f, 4.5f, 5.6f, 6.7f, 7.8f, 8.9f, 4.2f};
  // Dest completely overwritten, except for last value: we only mix(8)

  float expect[] = {-1.0f, -1.0f,       -0.50390625f, -0.000030517578f,
                    0.0f,  0.50390625f, 0.99996948f,  1.0f,
                    4.2f};

  audio::OutputFormatterPtr output_formatter =
      SelectOutputFormatter(fuchsia::media::AudioSampleFormat::FLOAT, 1);

  output_formatter->ProduceOutput(accum, reinterpret_cast<void*>(dest),
                                  fbl::count_of(accum));
  EXPECT_TRUE(CompareBuffers(dest, expect, fbl::count_of(dest)));
}

// Are 8-bit output buffers correctly silenced? Do we stop when we should?
TEST(PassThru, Output_8_Silence) {
  uint8_t dest[] = {12, 23, 34, 45, 56, 67, 78};
  // should be overwritten, except for the last value: we only fill(6)

  OutputFormatterPtr output_formatter =
      SelectOutputFormatter(fuchsia::media::AudioSampleFormat::UNSIGNED_8, 2);
  ASSERT_NE(nullptr, output_formatter);

  output_formatter->FillWithSilence(reinterpret_cast<void*>(dest),
                                    (fbl::count_of(dest) - 1) / 2);
  EXPECT_TRUE(CompareBufferToVal(dest, static_cast<uint8_t>(0x80),
                                 fbl::count_of(dest) - 1));
  EXPECT_EQ(dest[fbl::count_of(dest) - 1], 78);  // this val survives
}

// Are 16-bit output buffers correctly silenced? Do we stop when we should?
TEST(PassThru, Output_16_Silence) {
  int16_t dest[] = {1234, 2345, 3456, 4567, 5678, 6789, 7890};
  // should be overwritten, except for the last value: we only fill(6)

  OutputFormatterPtr output_formatter =
      SelectOutputFormatter(fuchsia::media::AudioSampleFormat::SIGNED_16, 3);
  ASSERT_NE(output_formatter, nullptr);

  output_formatter->FillWithSilence(reinterpret_cast<void*>(dest),
                                    (fbl::count_of(dest) - 1) / 3);
  EXPECT_TRUE(CompareBufferToVal(dest, static_cast<int16_t>(0),
                                 fbl::count_of(dest) - 1));
  EXPECT_EQ(dest[fbl::count_of(dest) - 1], 7890);  // should survive
}

// Are float output buffers correctly silenced? Do we stop when we should?
TEST(PassThru, Output_Float_Silence) {
  float dest[] = {1.2f, 2.3f, 3.4f, 4.5f, 5.6f, 6.7f, 7.8f};
  // should be overwritten, except for the last value: we only fill(6)

  audio::OutputFormatterPtr output_formatter =
      SelectOutputFormatter(fuchsia::media::AudioSampleFormat::FLOAT, 2);
  ASSERT_NE(output_formatter, nullptr);

  output_formatter->FillWithSilence(reinterpret_cast<void*>(dest),
                                    (fbl::count_of(dest) - 1) / 2);
  EXPECT_TRUE(CompareBufferToVal(dest, static_cast<float>(0.0f),
                                 fbl::count_of(dest) - 1));
  EXPECT_EQ(dest[fbl::count_of(dest) - 1], 7.8f);  // this val survives
}

}  // namespace test
}  // namespace audio
}  // namespace media

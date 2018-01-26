// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include "audio_analysis.h"
#include "garnet/bin/media/audio_server/gain.h"
#include "garnet/bin/media/audio_server/platform/generic/mixer.h"
#include "garnet/bin/media/audio_server/platform/generic/output_formatter.h"
#include "gtest/gtest.h"
#include "mixer_tests_shared.h"

namespace media {
namespace test {

//
// DataFormats tests - can we "connect the dots" from data source to data
// destination, for any permutation of format/configuration settings
//
// Create PointSampler objects for incoming buffers of type int16
// If the source sample rate is an integer-multiple of the destination rate
// (including 1, for pass-thru resampling), select the PointSampler
TEST(DataFormats, PointSampler16) {
  EXPECT_NE(nullptr,
            SelectMixer(AudioSampleFormat::SIGNED_16, 1, 24000, 1, 24000));
  EXPECT_NE(nullptr,
            SelectMixer(AudioSampleFormat::SIGNED_16, 1, 44100, 2, 11025));
}

// Create PointSampler objects for incoming buffers of type uint8
// If the source sample rate is an integer-multiple of the destination rate
// (including 1, for pass-thru resampling), select the PointSampler
TEST(DataFormats, PointSampler8) {
  EXPECT_NE(nullptr,
            SelectMixer(AudioSampleFormat::UNSIGNED_8, 2, 32000, 1, 16000));
  EXPECT_NE(nullptr,
            SelectMixer(AudioSampleFormat::UNSIGNED_8, 4, 48000, 4, 48000));
}

// Create PointSampler objects for other formats of incoming buffers
// This is not expected to work, as these are not yet implemented
// If the source sample rate is an integer-multiple of the destination rate
// (including 1, for pass-thru resampling), select the PointSampler
TEST(DataFormats, PointSamplerOther) {
  // TODO(mpuryear): Maybe NONE/ANY should ASSERT, as success is never possible
  EXPECT_EQ(nullptr, SelectMixer(AudioSampleFormat::NONE, 1, 44100, 1, 44100));
  EXPECT_EQ(nullptr, SelectMixer(AudioSampleFormat::ANY, 1, 44100, 2, 22050));

  EXPECT_EQ(nullptr,
            SelectMixer(AudioSampleFormat::SIGNED_24_IN_32, 2, 8000, 1, 8000));
  EXPECT_EQ(nullptr, SelectMixer(AudioSampleFormat::FLOAT, 2, 48000, 2, 16000));
}

// Create LinearSampler objects for incoming buffers of type int16
// If the source sample rate is NOT an integer-multiple of the destination rate
// (including when the destination is an integer multiple of the SOURCE rate),
// select the LinearSampler
TEST(DataFormats, LinearSampler16) {
  EXPECT_NE(nullptr,
            SelectMixer(AudioSampleFormat::SIGNED_16, 2, 16000, 2, 48000));
  EXPECT_NE(nullptr,
            SelectMixer(AudioSampleFormat::SIGNED_16, 2, 44100, 1, 48000));
  EXPECT_NE(nullptr,
            SelectMixer(AudioSampleFormat::SIGNED_16, 8, 48000, 8, 44100));
}

// Create LinearSampler objects for incoming buffers of type uint8
// If the source sample rate is NOT an integer-multiple of the destination rate
// (including when the destination is an integer multiple of the SOURCE rate),
// select the LinearSampler
TEST(DataFormats, LinearSampler8) {
  EXPECT_NE(nullptr,
            SelectMixer(AudioSampleFormat::UNSIGNED_8, 1, 22050, 2, 44100));
  EXPECT_NE(nullptr,
            SelectMixer(AudioSampleFormat::UNSIGNED_8, 2, 44100, 1, 48000));
}

// Create LinearSampler objects for other formats of incoming buffers
// This is not expected to work, as these are not yet implemented
// If the source sample rate is NOT an integer-multiple of the destination rate
// (including when the destination is an integer multiple of the SOURCE rate),
// select the LinearSampler
TEST(DataFormats, LinearSamplerOther) {
  // TODO(mpuryear): Maybe NONE/ANY should ASSERT, as success is never possible
  EXPECT_EQ(nullptr, SelectMixer(AudioSampleFormat::NONE, 1, 24000, 1, 44100));
  EXPECT_EQ(nullptr, SelectMixer(AudioSampleFormat::ANY, 1, 44100, 2, 48000));

  EXPECT_EQ(nullptr,
            SelectMixer(AudioSampleFormat::SIGNED_24_IN_32, 2, 8000, 1, 11025));
  EXPECT_EQ(nullptr, SelectMixer(AudioSampleFormat::FLOAT, 2, 48000, 2, 44100));
}

// Create a NoOpMixer object
// If the destination details are unspecified (nullptr), select NoOpMixer
TEST(DataFormats, NoOpMixer) {
  AudioMediaTypeDetailsPtr src_details = AudioMediaTypeDetails::New();
  src_details->sample_format = AudioSampleFormat::SIGNED_16;
  src_details->channels = 2;
  src_details->frames_per_second = 48000;

  EXPECT_NE(nullptr, audio::Mixer::Select(src_details, nullptr));
}

// Create OutputFormatter objects for outgoing buffers of type int16
TEST(DataFormats, OutputFormatter16) {
  EXPECT_NE(nullptr, SelectOutputFormatter(AudioSampleFormat::SIGNED_16, 4));
}

// Create OutputFormatter objects for outgoing buffers of type uint8
TEST(DataFormats, OutputFormatter8) {
  EXPECT_NE(nullptr, SelectOutputFormatter(AudioSampleFormat::UNSIGNED_8, 2));
}

// Create OutputFormatter objects for other output formats
// This is not expected to work, as these are not yet implemented
TEST(DataFormats, OutputFormatterOther) {
  EXPECT_EQ(nullptr, SelectOutputFormatter(AudioSampleFormat::NONE, 1));
  EXPECT_EQ(nullptr, SelectOutputFormatter(AudioSampleFormat::ANY, 2));
  EXPECT_EQ(nullptr,
            SelectOutputFormatter(AudioSampleFormat::SIGNED_24_IN_32, 3));
  EXPECT_EQ(nullptr, SelectOutputFormatter(AudioSampleFormat::FLOAT, 4));
}

//
// PassThru tests - can audio data flow through the different stages in our
// system without being altered, using numerous possible configurations?
//
// Can 16-bit values flow unchanged (2-2, N-N) thru the system? With 1:1 frame
// conversion, unity scale and no accumulation, we expect bit-equality.
TEST(PassThru, Signed16Mix) {
  std::array<int16_t, 8> src_buf = {-0x8000, 0x7FFF, -0x67A7, 0x4D4D,
                                    -0x123,  0,      0x2600,  -0x2DCB};
  std::array<int32_t, 8> accum_buf;
  std::array<int32_t, 8> expect_buf = {-0x8000, 0x7FFF, -0x67A7, 0x4D4D,
                                       -0x123,  0,      0x2600,  -0x2DCB};

  audio::MixerPtr mixer =
      SelectMixer(AudioSampleFormat::SIGNED_16, 2, 48000, 2, 48000);
  DoMix(std::move(mixer), src_buf.data(), accum_buf.data(), false, 4);
  EXPECT_TRUE(CompareBuffers<int32_t>(accum_buf.data(), expect_buf.data(), 8));

  accum_buf.fill(0);
  mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 4, 48000, 4, 48000);
  DoMix(std::move(mixer), src_buf.data(), accum_buf.data(), false, 2);
  EXPECT_TRUE(CompareBuffers<int32_t>(accum_buf.data(), expect_buf.data(), 8));
}

// Can 8-bit values flow unchanged (1-1, N-N) thru the system? With 1:1 frame
// conversion, unity scale and no accumulation, we expect bit-equality.
TEST(PassThru, Unsigned8Mix) {
  std::array<uint8_t, 8> src_buf = {0x00, 0xFF, 0x27, 0xCD,
                                    0x7F, 0x80, 0xA6, 0x6D};
  std::array<int32_t, 8> accum_buf;
  std::array<int32_t, 8> expect_buf = {-0x8000, 0x7F00, -0x5900, 0x4D00,
                                       -0x0100, 0,      0x2600,  -0x1300};

  audio::MixerPtr mixer =
      SelectMixer(AudioSampleFormat::UNSIGNED_8, 1, 48000, 1, 48000);
  DoMix(std::move(mixer), src_buf.data(), accum_buf.data(), false, 8);
  EXPECT_TRUE(CompareBuffers<int32_t>(accum_buf.data(), expect_buf.data(), 8));

  mixer = SelectMixer(AudioSampleFormat::UNSIGNED_8, 8, 48000, 8, 48000);
  DoMix(std::move(mixer), src_buf.data(), accum_buf.data(), false, 1);
  EXPECT_TRUE(CompareBuffers<int32_t>(accum_buf.data(), expect_buf.data(), 8));
}

// Does NoOp mixer behave as expected? (not update offsets, nor touch buffers)
TEST(PassThru, ThrottleMix) {
  AudioMediaTypeDetailsPtr src_details = AudioMediaTypeDetails::New();
  src_details->sample_format = AudioSampleFormat::SIGNED_16;
  src_details->channels = 1;
  src_details->frames_per_second = 48000;

  audio::MixerPtr no_op_mixer = audio::Mixer::Select(src_details, nullptr);
  EXPECT_NE(nullptr, no_op_mixer);

  uint32_t dst_frames = 2, dst_offset = 0;
  uint32_t src_frames = dst_frames << audio::kPtsFractionalBits;
  int32_t frac_src_offset = 0;
  uint32_t step_size = media::audio::Mixer::FRAC_ONE;
  audio::Gain::AScale scale = audio::Gain::kUnityScale;

  std::array<int16_t, 2> src_buf = {32767, -32768};
  std::array<int32_t, 2> accum_buf = {-1, 42};

  bool mix_result = no_op_mixer->Mix(accum_buf.data(), dst_frames, &dst_offset,
                                     src_buf.data(), src_frames,
                                     &frac_src_offset, step_size, scale, false);

  EXPECT_FALSE(mix_result);
  EXPECT_EQ(0u, dst_offset);
  EXPECT_EQ(0, frac_src_offset);
  EXPECT_EQ(-1, accum_buf[0]);
  EXPECT_EQ(42, accum_buf[1]);
}

// Are all valid data values passed correctly to 16-bit outputs
TEST(PassThru, MonoToStereo) {
  std::array<int16_t, 6> src_buf = {-32768, -16383, -1, 0, 1, 32767};
  std::array<int32_t, 6 * 2> accum_buf;
  std::array<int32_t, 6 * 2> expect_buf = {
      -32768, -32768, -16383, -16383, -1, -1, 0, 0, 1, 1, 32767, 32767};

  audio::MixerPtr mixer =
      SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 2, 48000);
  DoMix(std::move(mixer), src_buf.data(), accum_buf.data(), false, 6);
  EXPECT_TRUE(
      CompareBuffers<int32_t>(accum_buf.data(), expect_buf.data(), 6 * 2));
}

// Do we correctly mix stereo to mono, when channels sum to exactly zero
TEST(PassThru, StereoToMonoZero) {
  std::array<int16_t, 6 * 2> src_buf = {
      32767, -32767, -23130, 23130, 0, 0, 1, -1, -13107, 13107, 3855, -3855};
  std::array<int32_t, 6> accum_buf;

  audio::MixerPtr mixer =
      SelectMixer(AudioSampleFormat::SIGNED_16, 2, 48000, 1, 48000);

  DoMix(std::move(mixer), src_buf.data(), accum_buf.data(), false, 6);
  EXPECT_TRUE(CompareBufferToVal<int32_t>(accum_buf.data(), 0, 6));
}

// Do we correctly mix stereo->mono (shift? divide? truncate? round? dither?)
// Our 2:1 folddown shifts (not div+round); leading to slight negative bias.
// TODO(mpuryear): Adjust the expected values below, after we fix MTWN-81.
TEST(PassThru, StereoToMonoRound) {
  // pairs: positive even, neg even, pos odd, neg odd, pos limit, neg limit
  std::array<int16_t, 6 * 2> src_buf = {-21,   12021, 123,    -345,
                                        -1000, 1005,  -4155,  -7000,
                                        32767, 32767, -32768, -32768};

  std::array<int32_t, 6> accum_buf = {-123, 234, -345, 456, -567, 678};
  std::array<int32_t, 6> expect_buf = {6000, -111, 2, -5578, 32767, -32768};

  audio::MixerPtr mixer =
      SelectMixer(AudioSampleFormat::SIGNED_16, 2, 48000, 1, 48000);
  DoMix(std::move(mixer), src_buf.data(), accum_buf.data(), false, 6);
  EXPECT_TRUE(CompareBuffers<int32_t>(accum_buf.data(), expect_buf.data(), 6));
}

// Do we obey the 'accumulate' flag if mixing into existing accumulated data
TEST(PassThru, Accumulate) {
  std::array<int16_t, 2 * 2> src_buf = {-4321, 2345, 6789, -8765};
  std::array<int32_t, 2 * 2> accum_buf = {22222, 11111, -5555, 9630};
  std::array<int32_t, 2 * 2> expect_buf = {17901, 13456, 1234, 865};

  audio::MixerPtr mixer =
      SelectMixer(AudioSampleFormat::SIGNED_16, 2, 48000, 2, 48000);
  DoMix(std::move(mixer), src_buf.data(), accum_buf.data(), true, 2);
  EXPECT_TRUE(
      CompareBuffers<int32_t>(accum_buf.data(), expect_buf.data(), 2 * 2));

  expect_buf = {-4321, 2345, 6789, -8765};
  mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 2, 48000, 2, 48000);
  DoMix(std::move(mixer), src_buf.data(), accum_buf.data(), false, 2);
  EXPECT_TRUE(
      CompareBuffers<int32_t>(accum_buf.data(), expect_buf.data(), 2 * 2));
}

// Are all valid data values passed correctly to 16-bit outputs
TEST(PassThru, OutputFormatterProduce16) {
  std::array<int32_t, 8> accum_buf = {-32896, -32768, -16512, -1,
                                      0,      16512,  32767,  32768};
  // hex vals: -0x8080 -0x8000 -0x4080 -1  0  0x4080 0x7FFF 0x8000

  std::array<int16_t, 9> dst_buf = {0123, 1234, 2345, 3456, 4567,
                                    5678, 6789, 7890, -42};
  // Dest buffer is overwritten, except for last value: we only mix(8)

  std::array<int16_t, 9> expect_buf = {-32768, -32768, -16512, -1, 0,
                                       16512,  32767,  32767,  -42};

  audio::OutputFormatterPtr output_formatter =
      SelectOutputFormatter(AudioSampleFormat::SIGNED_16, 2);

  output_formatter->ProduceOutput(
      accum_buf.data(), reinterpret_cast<void*>(dst_buf.data()), 8 / 2);
  EXPECT_TRUE(CompareBuffers<int16_t>(dst_buf.data(), expect_buf.data(), 8));
}

// Are all valid data values passed correctly to 8-bit outputs
// Important: OutputFormatter<uint8> truncates (not rounds).
// TODO(mpuryear): Change expectations to correct vals when we fix MTWN-84.
TEST(PassThru, OutputFormatterProduce8) {
  std::array<int32_t, 8> accum_buf = {-32896, -32768, -16512, -1,
                                      0,      16512,  32767,  32768};
  // hex vals: -0x8080 -0x8000 -0x4080 -1  0  0x4080 0x7FFF 0x8000
  //              ^^^^  we clamp these vals to uint8 limits   ^^^^
  std::array<uint8_t, 9> dst_buf = {12, 23, 34, 45, 56, 67, 78, 89, 42};
  // Dest completely overwritten, except for last value: we only mix(8)
  std::array<uint8_t, 9> expect_buf = {0x0,  0x0,  0x3F, 0x7F, 0x80,
                                       0xC0, 0xFF, 0xFF, 42};

  audio::OutputFormatterPtr output_formatter =
      SelectOutputFormatter(AudioSampleFormat::UNSIGNED_8, 1);

  output_formatter->ProduceOutput(accum_buf.data(),
                                  reinterpret_cast<void*>(dst_buf.data()), 8);
  EXPECT_TRUE(CompareBuffers<uint8_t>(dst_buf.data(), expect_buf.data(), 8));
}

// Are 16-bit output buffers correctly silenced? Do we stop when we should?
TEST(PassThru, OutputFormatterSilence16) {
  std::array<int16_t, (2 * 3) + 1> dst_buf = {1234, 2345, 3456, 4567,
                                              5678, 6789, 7890};
  // should be overwritten, except for the last value: we only fill(6)

  audio::OutputFormatterPtr output_formatter =
      SelectOutputFormatter(AudioSampleFormat::SIGNED_16, 3);
  ASSERT_NE(output_formatter, nullptr);

  output_formatter->FillWithSilence(reinterpret_cast<void*>(dst_buf.data()), 2);
  EXPECT_TRUE(CompareBufferToVal<int16_t>(dst_buf.data(), 0, 2 * 3));
  EXPECT_EQ(dst_buf[2 * 3], 7890);  // this previous value should survive
}

// Are 8-bit output buffers correctly silenced? Do we stop when we should?
TEST(PassThru, OutputFormatterSilence8) {
  std::array<uint8_t, (3 * 2) + 1> dst_buf = {12, 23, 34, 45, 56, 67, 78};
  // should be overwritten, except for the last value: we only fill(6)

  audio::OutputFormatterPtr output_formatter =
      SelectOutputFormatter(AudioSampleFormat::UNSIGNED_8, 2);
  ASSERT_NE(nullptr, output_formatter);

  output_formatter->FillWithSilence(reinterpret_cast<void*>(dst_buf.data()), 3);
  EXPECT_TRUE(CompareBufferToVal<uint8_t>(dst_buf.data(), 0x80, 3 * 2));
  EXPECT_EQ(dst_buf[2 * 3], 78);  // this previous value should survive
}

}  // namespace test
}  // namespace media
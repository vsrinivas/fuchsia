// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>

#include "audio_analysis.h"
#include "lib/fxl/logging.h"
#include "mixer_tests_shared.h"

namespace media {
namespace test {

//
// DataFormats tests - can we "connect the dots" from data source to data
// destination, for any permutation of format/configuration settings
//
// If the source sample rate is an integer-multiple of the destination rate
// (including 1, for pass-thru resampling), select the PointSampler
//
// Create PointSampler objects for incoming buffers of type int16
TEST(DataFormats, PointSampler16) {
  EXPECT_NE(nullptr,
            SelectMixer(AudioSampleFormat::SIGNED_16, 1, 24000, 1, 24000));
  EXPECT_NE(nullptr,
            SelectMixer(AudioSampleFormat::SIGNED_16, 1, 44100, 2, 11025));
}

// Create PointSampler objects for incoming buffers of type uint8
TEST(DataFormats, PointSampler8) {
  EXPECT_NE(nullptr,
            SelectMixer(AudioSampleFormat::UNSIGNED_8, 2, 32000, 1, 16000));
  EXPECT_NE(nullptr,
            SelectMixer(AudioSampleFormat::UNSIGNED_8, 4, 48000, 4, 48000));
}

// Create PointSampler objects for other formats of incoming buffers
// This is not expected to work, as these are not yet implemented
TEST(DataFormats, PointSamplerOther) {
  EXPECT_EQ(nullptr,
            SelectMixer(AudioSampleFormat::SIGNED_24_IN_32, 2, 8000, 1, 8000));
  EXPECT_EQ(nullptr, SelectMixer(AudioSampleFormat::FLOAT, 2, 48000, 2, 16000));
}

// If the source sample rate is NOT an integer-multiple of the destination rate
// (including when the destination is an integer multiple of the SOURCE rate),
// select the LinearSampler
//
// Create LinearSampler objects for incoming buffers of type int16
TEST(DataFormats, LinearSampler16) {
  EXPECT_NE(nullptr,
            SelectMixer(AudioSampleFormat::SIGNED_16, 2, 16000, 2, 48000));
  EXPECT_NE(nullptr,
            SelectMixer(AudioSampleFormat::SIGNED_16, 2, 44100, 1, 48000));
  EXPECT_NE(nullptr,
            SelectMixer(AudioSampleFormat::SIGNED_16, 8, 48000, 8, 44100));
}

// Create LinearSampler objects for incoming buffers of type uint8
TEST(DataFormats, LinearSampler8) {
  EXPECT_NE(nullptr,
            SelectMixer(AudioSampleFormat::UNSIGNED_8, 1, 22050, 2, 44100));
  EXPECT_NE(nullptr,
            SelectMixer(AudioSampleFormat::UNSIGNED_8, 2, 44100, 1, 48000));
}

// Create LinearSampler objects for other formats of incoming buffers
// This is not expected to work, as these are not yet implemented
TEST(DataFormats, LinearSamplerOther) {
  EXPECT_EQ(nullptr,
            SelectMixer(AudioSampleFormat::SIGNED_24_IN_32, 2, 8000, 1, 11025));
  EXPECT_EQ(nullptr, SelectMixer(AudioSampleFormat::FLOAT, 2, 48000, 2, 44100));
}

// If the destination details are unspecified (nullptr), select NoOpMixer
//
// Create a NoOpMixer object
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
  int16_t source[] = {-0x8000, 0x7FFF, -0x67A7, 0x4D4D,
                      -0x123,  0,      0x2600,  -0x2DCB};
  int32_t accum[8];
  int32_t expect[] = {-0x8000, 0x7FFF, -0x67A7, 0x4D4D,
                      -0x123,  0,      0x2600,  -0x2DCB};

  // Try in 2-channel mode
  audio::MixerPtr mixer =
      SelectMixer(AudioSampleFormat::SIGNED_16, 2, 48000, 2, 48000);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum) / 2);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  ::memset(accum, 0, sizeof(accum));
  // Now try in 4-channel mode
  mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 4, 48000, 4, 48000);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum) / 4);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));
}

// Can 8-bit values flow unchanged (1-1, N-N) thru the system? With 1:1 frame
// conversion, unity scale and no accumulation, we expect bit-equality.
TEST(PassThru, Unsigned8Mix) {
  uint8_t source[] = {0x00, 0xFF, 0x27, 0xCD, 0x7F, 0x80, 0xA6, 0x6D};
  int32_t accum[8];
  int32_t expect[] = {-0x8000, 0x7F00, -0x5900, 0x4D00,
                      -0x0100, 0,      0x2600,  -0x1300};

  audio::MixerPtr mixer =
      SelectMixer(AudioSampleFormat::UNSIGNED_8, 1, 48000, 1, 48000);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum));
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  mixer = SelectMixer(AudioSampleFormat::UNSIGNED_8, 8, 48000, 8, 48000);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum) / 8);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));
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

  int16_t source[] = {32767, -32768};
  int32_t accum[] = {-1, 42};

  bool mix_result =
      no_op_mixer->Mix(accum, dst_frames, &dst_offset, source, src_frames,
                       &frac_src_offset, step_size, scale, false);

  EXPECT_FALSE(mix_result);
  EXPECT_EQ(0u, dst_offset);
  EXPECT_EQ(0, frac_src_offset);
  EXPECT_EQ(-1, accum[0]);
  EXPECT_EQ(42, accum[1]);
}

// Are all valid data values passed correctly to 16-bit outputs
TEST(PassThru, MonoToStereo) {
  int16_t source[] = {-32768, -16383, -1, 0, 1, 32767};
  int32_t accum[6 * 2];
  int32_t expect[] = {-32768, -32768, -16383, -16383, -1,    -1,
                      0,      0,      1,      1,      32767, 32767};

  audio::MixerPtr mixer =
      SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 2, 48000);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum) / 2);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));
}

// Do we correctly mix stereo to mono, when channels sum to exactly zero
TEST(PassThru, StereoToMonoZero) {
  int16_t source[] = {32767, -32767, -23130, 23130, 0,    0,
                      1,     -1,     -13107, 13107, 3855, -3855};
  int32_t accum[6];

  audio::MixerPtr mixer =
      SelectMixer(AudioSampleFormat::SIGNED_16, 2, 48000, 1, 48000);

  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum));
  EXPECT_TRUE(CompareBufferToVal(accum, 0, fbl::count_of(accum)));
}

// Do we correctly mix stereo->mono (shift? divide? truncate? round? dither?)
// Our 2:1 folddown shifts (not div+round); leading to slight negative bias.
// TODO(mpuryear): Adjust the expected values below, after we fix MTWN-81.
TEST(PassThru, StereoToMonoRound) {
  // pairs: positive even, neg even, pos odd, neg odd, pos limit, neg limit
  int16_t source[] = {-21,   12021, 123,   -345,  -1000,  1005,
                      -4155, -7000, 32767, 32767, -32768, -32768};

  int32_t accum[] = {-123, 234, -345, 456, -567, 678};
  int32_t expect[] = {6000, -111, 2, -5578, 32767, -32768};

  audio::MixerPtr mixer =
      SelectMixer(AudioSampleFormat::SIGNED_16, 2, 48000, 1, 48000);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum));
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));
}

// Do we obey the 'accumulate' flag if mixing into existing accumulated data
TEST(PassThru, Accumulate) {
  int16_t source[] = {-4321, 2345, 6789, -8765};
  int32_t accum[] = {22222, 11111, -5555, 9630};
  int32_t expect[] = {17901, 13456, 1234, 865};

  audio::MixerPtr mixer =
      SelectMixer(AudioSampleFormat::SIGNED_16, 2, 48000, 2, 48000);
  DoMix(std::move(mixer), source, accum, true, fbl::count_of(accum) / 2);
  EXPECT_TRUE(CompareBuffers(accum, expect, fbl::count_of(accum)));

  int32_t expect2[] = {-4321, 2345, 6789, -8765};
  mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 2, 48000, 2, 48000);
  DoMix(std::move(mixer), source, accum, false, fbl::count_of(accum) / 2);
  EXPECT_TRUE(CompareBuffers(accum, expect2, fbl::count_of(accum)));
}

// Are all valid data values passed correctly to 16-bit outputs
TEST(PassThru, OutputFormatterProduce16) {
  int32_t accum[] = {-32896, -32768, -16512, -1, 0, 16512, 32767, 32768};
  // hex vals:       -x8080  -x8000  -x4080  -1  0  x4080  x7FFF  x8000

  int16_t dest[] = {0123, 1234, 2345, 3456, 4567, 5678, 6789, 7890, -42};
  // Dest buffer is overwritten, EXCEPT for last value: we only mix(8)

  int16_t expect[] = {-32768, -32768, -16512, -1, 0, 16512, 32767, 32767, -42};

  audio::OutputFormatterPtr output_formatter =
      SelectOutputFormatter(AudioSampleFormat::SIGNED_16, 2);

  output_formatter->ProduceOutput(accum, reinterpret_cast<void*>(dest),
                                  fbl::count_of(accum) / 2);
  EXPECT_TRUE(CompareBuffers(dest, expect, fbl::count_of(dest)));
}

// Are all valid data values passed correctly to 8-bit outputs
// Important: OutputFormatter<uint8> truncates (not rounds).
// TODO(mpuryear): Change expectations to correct vals when we fix MTWN-84.
TEST(PassThru, OutputFormatterProduce8) {
  int32_t accum[] = {-32896, -32768, -16512, -1, 0, 16512, 32767, 32768};
  // hex vals:       -x8080  -x8000  -x4080  -1  0  x4080  x7FFF  x8000
  //                   ^^^^  we clamp these vals to uint8 limits   ^^^^

  uint8_t dest[] = {12, 23, 34, 45, 56, 67, 78, 89, 42};
  // Dest completely overwritten, except for last value: we only mix(8)

  uint8_t expect[] = {0x0, 0x0, 0x3F, 0x7F, 0x80, 0xC0, 0xFF, 0xFF, 42};

  audio::OutputFormatterPtr output_formatter =
      SelectOutputFormatter(AudioSampleFormat::UNSIGNED_8, 1);

  output_formatter->ProduceOutput(accum, reinterpret_cast<void*>(dest),
                                  fbl::count_of(accum));
  EXPECT_TRUE(CompareBuffers(dest, expect, fbl::count_of(dest)));
}

// Are 16-bit output buffers correctly silenced? Do we stop when we should?
TEST(PassThru, OutputFormatterSilence16) {
  int16_t dest[] = {1234, 2345, 3456, 4567, 5678, 6789, 7890};
  // should be overwritten, except for the last value: we only fill(6)

  audio::OutputFormatterPtr output_formatter =
      SelectOutputFormatter(AudioSampleFormat::SIGNED_16, 3);
  ASSERT_NE(output_formatter, nullptr);

  output_formatter->FillWithSilence(reinterpret_cast<void*>(dest),
                                    (fbl::count_of(dest) - 1) / 3);
  EXPECT_TRUE(CompareBufferToVal(dest, static_cast<int16_t>(0),
                                 fbl::count_of(dest) - 1));
  EXPECT_EQ(dest[fbl::count_of(dest) - 1],
            7890);  // this previous value should survive
}

// Are 8-bit output buffers correctly silenced? Do we stop when we should?
TEST(PassThru, OutputFormatterSilence8) {
  uint8_t dest[] = {12, 23, 34, 45, 56, 67, 78};
  // should be overwritten, except for the last value: we only fill(6)

  audio::OutputFormatterPtr output_formatter =
      SelectOutputFormatter(AudioSampleFormat::UNSIGNED_8, 2);
  ASSERT_NE(nullptr, output_formatter);

  output_formatter->FillWithSilence(reinterpret_cast<void*>(dest),
                                    (fbl::count_of(dest) - 1) / 2);
  EXPECT_TRUE(CompareBufferToVal(dest, static_cast<uint8_t>(0x80),
                                 fbl::count_of(dest) - 1));
  EXPECT_EQ(dest[fbl::count_of(dest) - 1], 78);  // this val survives
}

//
// Pass-thru Noise-Floor tests
//
// These tests determine our best-case audio quality/fidelity, in the absence of
// any gain, interpolation/SRC, mixing, reformatting or other processing. These
// tests are done with a single 1kHz tone, and provide a baseline from which we
// can measure any changes in sonic quality caused by other mixer stages.
//
// In performing all of our audio analysis tests with a specific buffer length,
// We can choose input sinusoids with frequencies that perfectly fit within
// those buffers (eliminating the need for FFT windowing). The reference
// frequency below was specifically chosen as an approximation of a 1kHz tone,
// assuming a 48kHz sample rate.
constexpr uint32_t kNoiseFloorBufSize = 65536;
constexpr uint32_t kReferenceFreq = 1363;  // 1kHz equivalent (65536/48000)

template <typename T>
double MeasureSourceNoiseFloor(double* magn_other_db) {
  audio::MixerPtr mixer;

  if (std::is_same<T, uint8_t>::value) {
    mixer = SelectMixer(AudioSampleFormat::UNSIGNED_8, 1, 48000, 1, 48000);
  } else if (std::is_same<T, int16_t>::value) {
    mixer = SelectMixer(AudioSampleFormat::SIGNED_16, 1, 48000, 1, 48000);
  } else {
    FXL_DCHECK(false) << "Unsupported source format";
  }

  const double amplitude = (std::is_same<T, uint8_t>::value)
                               ? std::numeric_limits<int8_t>::max()
                               : std::numeric_limits<int16_t>::max();

  // Populate source buffer; mix it (pass-thru) to accumulation buffer
  std::vector<T> source(kNoiseFloorBufSize);
  AccumCosine(source.data(), kNoiseFloorBufSize, kReferenceFreq, amplitude, 0.0,
              false);

  std::vector<int32_t> accum(kNoiseFloorBufSize);
  uint32_t dst_offset = 0;
  int32_t frac_src_offset = 0;
  mixer->Mix(accum.data(), kNoiseFloorBufSize, &dst_offset, source.data(),
             kNoiseFloorBufSize << audio::kPtsFractionalBits, &frac_src_offset,
             media::audio::Mixer::FRAC_ONE, audio::Gain::kUnityScale, false);
  EXPECT_EQ(kNoiseFloorBufSize, dst_offset);
  EXPECT_EQ(
      static_cast<int32_t>(kNoiseFloorBufSize << audio::kPtsFractionalBits),
      frac_src_offset);

  // Copy result to double-float buffer, FFT (freq-analyze) it at high-res
  double magn_signal, magn_other;
  MeasureAudioFreq(accum.data(), kNoiseFloorBufSize, kReferenceFreq,
                   &magn_signal, &magn_other);

  // Calculate Signal-to-Noise-And-Distortion (SINAD)
  *magn_other_db = ValToDb(magn_signal / magn_other);

  // Calculate magnitude of primary signal strength
  if (std::is_same<T, uint8_t>::value) {
    magn_signal /= 256.0;
  }
  return ValToDb(magn_signal / amplitude);
}

// Measure Frequency Response and SINAD for 1kHz sine from 16bit source.
TEST(NoiseFloor, 16Bit_Source) {
  double magn_signal_db =
      MeasureSourceNoiseFloor<int16_t>(&floor_db_16bit_source);

  EXPECT_GE(magn_signal_db, -0.001) << "Test signal magnitude out of range";
  EXPECT_LE(magn_signal_db, 0.001) << "Test signal magnitude out of range";

  EXPECT_GE(floor_db_16bit_source, 98.0) << "Noise level out of range";
}

// Measure Frequency Response and SINAD for 1kHz sine from 8-bit source.
TEST(NoiseFloor, 8Bit_Source) {
  double magn_signal_db =
      MeasureSourceNoiseFloor<uint8_t>(&floor_db_8bit_source);

  EXPECT_GE(magn_signal_db, -0.1) << "Test signal magnitude out of range";
  EXPECT_LE(magn_signal_db, 0.1) << "Test signal magnitude out of range";

  EXPECT_GE(floor_db_8bit_source, 49.0) << "Noise level out of range";
}

template <typename T>
double MeasureOutputNoiseFloor(double* magn_other_db) {
  audio::OutputFormatterPtr output_formatter;

  if (std::is_same<T, uint8_t>::value) {
    output_formatter = SelectOutputFormatter(AudioSampleFormat::UNSIGNED_8, 1);
  } else if (std::is_same<T, int16_t>::value) {
    output_formatter = SelectOutputFormatter(AudioSampleFormat::SIGNED_16, 1);
  } else {
    FXL_DCHECK(false) << "Unsupported source format";
  }

  const double amplitude = std::numeric_limits<int16_t>::max();

  // Populate accum buffer and output to destination buffer
  std::vector<int32_t> accum(kNoiseFloorBufSize);
  AccumCosine(accum.data(), kNoiseFloorBufSize, kReferenceFreq, amplitude, 0.0,
              false);

  std::vector<T> dest(kNoiseFloorBufSize);
  output_formatter->ProduceOutput(accum.data(), dest.data(),
                                  kNoiseFloorBufSize);

  // Copy result to double-float buffer, FFT (freq-analyze) it at high-res
  double magn_signal, magn_other;
  MeasureAudioFreq(dest.data(), kNoiseFloorBufSize, kReferenceFreq,
                   &magn_signal, &magn_other);

  // Calculate Signal-to-Noise-And-Distortion (SINAD)
  *magn_other_db = ValToDb(magn_signal / magn_other);

  if (std::is_same<T, uint8_t>::value) {
    magn_signal *= 256.0;
  }

  // Calculate magnitude of primary signal strength
  return ValToDb(magn_signal / amplitude);
}

// Measure Frequency Response and SINAD for 1kHz sine, to a 16bit output.
TEST(NoiseFloor, 16Bit_Output) {
  double magn_signal_db =
      MeasureOutputNoiseFloor<int16_t>(&floor_db_16bit_output);

  EXPECT_GE(magn_signal_db, -0.001) << "Test signal magnitude out of range";
  EXPECT_LE(magn_signal_db, 0.001) << "Test signal magnitude out of range";

  EXPECT_GE(floor_db_16bit_output, 98.0) << "Noise level out of range";
}

// Measure Frequency Response and SINAD for 1kHz sine, to an 8bit output.
TEST(NoiseFloor, 8Bit_Output) {
  double magn_signal_db =
      MeasureOutputNoiseFloor<uint8_t>(&floor_db_8bit_output);

  EXPECT_GE(magn_signal_db, -0.1) << "Test signal magnitude out of range";
  EXPECT_LE(magn_signal_db, 0.1) << "Test signal magnitude out of range";

  EXPECT_GE(floor_db_8bit_output, 45.0) << "Noise level out of range";
}

}  // namespace test
}  // namespace media

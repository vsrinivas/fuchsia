// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_SERVER_MIXER_TEST_MIXER_TESTS_SHARED_H_
#define GARNET_BIN_MEDIA_AUDIO_SERVER_MIXER_TEST_MIXER_TESTS_SHARED_H_

#include "garnet/bin/media/audio_server/gain.h"
#include "garnet/bin/media/audio_server/mixer/mixer.h"
#include "garnet/bin/media/audio_server/mixer/output_formatter.h"
#include "garnet/bin/media/audio_server/mixer/test/audio_analysis.h"
#include "gtest/gtest.h"

namespace media {
namespace audio {
namespace test {

//
// Subtest shared helper functions -- used by tests; can ASSERT on their own.
//

// Converts a gain multiplier (in fixed-pt 4.28) to decibels (in double floating
// point). Here, dB refers to Power, so 10x change is +20 dB (not +10dB).
inline double GainScaleToDb(Gain::AScale gain_scale) {
  return ValToDb(static_cast<double>(gain_scale) / Gain::kUnityScale);
}

// Find a suitable mixer for the provided format, channels and frame rates.
MixerPtr SelectMixer(fuchsia::media::AudioSampleFormat src_format,
                     uint32_t src_channels, uint32_t src_frame_rate,
                     uint32_t dst_channels, uint32_t dst_frame_rate,
                     Mixer::Resampler resampler = Mixer::Resampler::Default);

// OutputFormatters convert frames from accumulation format to dest format.
OutputFormatterPtr SelectOutputFormatter(
    fuchsia::media::AudioSampleFormat dst_format, uint32_t num_channels);

// When doing direct bit-for-bit comparisons in our tests, we must factor in the
// conversion that occurs, from non-float inputs into our internal accumulator's
// float format. For this reason, tests that previously simply input a 16-bit
// value at unity SRC and gain, expecting that same 16-bit value to be deposited
// into the accumulator, should now expect that value to be converted to a float
// value in the range of [-1.0, +1.0). With this in mind, and to remain flexible
// amidst other changes in pipeline width, our tests now specify any expected
// values at the higher-than-needed precision of 28-bit. (They also specify
// values in hexadecimal format in most cases, to make bit-shifted values more
// clear.)  A __28__bit__ precision for test data was specifically chosen to
// accomodate the transition we have now made to a float32 internal pipeline,
// with its 25 effective bits of [precision+sign].
//
// This shared function, then, normalizes data arrays into our float32 pipeline.
// Because inputs must be in the range of [-2^27 , 2^27 ], for all practical
// purposes it wants "int28" inputs, hence this function's unexpected name.
void NormalizeInt28ToPipelineBitwidth(float* source, uint32_t source_len);

// Related to the conversions discussed above, these constants are the expected
// amplitudes in the accumulator of full-scale signals in various input types.
// "int24", int16 and int8 have more negative values than positive ones. Note
// this difference between integer and float signals: to be linear without
// clipping, a full-scale int-based signal reaches its max (such as 0x7FFF) but
// not its min (such as -0x8000). Thus, for "int24", int16 and (u)int8 data
// types, we expect accum magnitudes less than what we expect for floats (1.0).
constexpr double kFullScaleInt8InputAmplitude =
    static_cast<double>(std::numeric_limits<int8_t>::max());
constexpr double kFullScaleInt8AccumAmplitude =  // 0.9921875
    kFullScaleInt8InputAmplitude / media::audio::kFloatToInt8;

constexpr double kFullScaleInt16InputAmplitude =
    static_cast<double>(std::numeric_limits<int16_t>::max());
constexpr double kFullScaleInt16AccumAmplitude =  // 0.999969482421875
    kFullScaleInt16InputAmplitude / media::audio::kFloatToInt16;

constexpr double kFullScaleInt24In32InputAmplitude =
    static_cast<double>(media::audio::kMaxInt24In32);
constexpr double kFullScaleInt24In32AccumAmplitude =  // 0.99999988079071045
    kFullScaleInt24In32InputAmplitude / media::audio::kFloatToInt24In32;

constexpr double kFullScaleFloatInputAmplitude = 1.0f;
constexpr double kFullScaleFloatAccumAmplitude = 1.0f;

// Use supplied mixer to mix (w/out rate conversion) from source to accumulator.
// TODO(mpuryear): refactor this so that tests just call mixer->Mix directly.
void DoMix(MixerPtr mixer, const void* src_buf, float* accum_buf,
           bool accumulate, int32_t num_frames,
           Gain::AScale mix_scale = Gain::kUnityScale);

}  // namespace test
}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_SERVER_MIXER_TEST_MIXER_TESTS_SHARED_H_

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIXER_TEST_MIXER_TESTS_SHARED_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIXER_TEST_MIXER_TESTS_SHARED_H_

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/audio_core/v1/mixer/gain.h"
#include "src/media/audio/audio_core/v1/mixer/mixer.h"
#include "src/media/audio/audio_core/v1/mixer/output_producer.h"
#include "src/media/audio/lib/analysis/analysis.h"
#include "src/media/audio/lib/format2/sample_converter.h"

namespace media::audio::test {

//
// Subtest shared helper functions -- used by tests; can ASSERT on their own.
//

// Find a suitable mixer for the provided format, channels and frame rates.
std::unique_ptr<Mixer> SelectMixer(fuchsia::media::AudioSampleFormat source_format,
                                   int32_t source_channels, int32_t source_frame_rate,
                                   int32_t dest_channels, int32_t dest_frame_rate,
                                   Mixer::Resampler resampler);

// OutputProducers convert frames from accumulation format to dest format.
std::unique_ptr<OutputProducer> SelectOutputProducer(fuchsia::media::AudioSampleFormat dest_format,
                                                     int32_t num_channels);

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
    kFullScaleInt8InputAmplitude / media_audio::kFloatToInt8;

constexpr double kFullScaleInt16InputAmplitude =
    static_cast<double>(std::numeric_limits<int16_t>::max());
constexpr double kFullScaleInt16AccumAmplitude =  // 0.999969482421875
    kFullScaleInt16InputAmplitude / media_audio::kFloatToInt16;

constexpr double kFullScaleInt24In32InputAmplitude =
    static_cast<double>(media_audio::kMaxInt24In32);
constexpr double kFullScaleInt24In32AccumAmplitude =  // 0.99999988079071045
    static_cast<double>(media_audio::kMaxInt24) / media_audio::kFloatToInt24;

constexpr double kFullScaleFloatInputAmplitude = 1.0f;
constexpr double kFullScaleFloatAccumAmplitude = 1.0f;

// Converts gain `scale` to decibels in double-precision.
inline double DoubleToDb(double scale) {
  return (scale > static_cast<double>(media_audio::kMinGainScale))
             ? std::log10(scale) * 20.0
             : static_cast<double>(media_audio::kMinGainDb);
}

// Use supplied mixer to mix (w/out rate conversion) from source to accumulator.
// TODO(mpuryear): refactor this so that tests just call mixer->Mix directly.
void DoMix(Mixer* mixer, const void* source_buf, float* accum_buf, bool accumulate,
           int64_t num_frames, float gain_db = 0.0f);

// Returns a pair of (format_amplitude, double_amplitude) where
// |format_amplitude| is the amplitude that should be applied in the given format |f|, and
// |double_amplitude| is the expected amplitude after translation from |f| to double.
std::pair<double, double> SampleFormatToAmplitudes(fuchsia::media::AudioSampleFormat f);

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_MIXER_TEST_MIXER_TESTS_SHARED_H_

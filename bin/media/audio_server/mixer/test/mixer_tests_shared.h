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
// left-shift biasing that is done while converting input data into the internal
// format of our accumulator. For this reason, tests that previously simply
// input a 16-bit value at unity SRC and gain, expecting that same 16-bit value
// to be deposited into the accumulator, would now expect that value to be
// left-shifted by some number of bits. With this in mind, and to remain
// flexible in the midst of changes in our pipeline width, our tests now specify
// any expected values at the higher-than-needed precision of 28-bit. (They also
// specify values in hexadecimal format in almost all cases, to make bit-shifted
// values slightly more clear.)  This precision of __28__bit__ test data was
// specifically chosen to accomodate a future transition to a float32 pipeline,
// which has 25 effective bits of [precision+sign].
//
// This shared function, then, is used to normalize data arrays down to the
// actual pipeline width, depending on the details of our processing pipeline.
void NormalizeInt28ToPipelineBitwidth(int32_t* source, uint32_t source_len);

// Use supplied mixer to mix (w/out rate conversion) from source to accumulator.
// TODO(mpuryear): refactor this so that tests just call mixer->Mix directly.
void DoMix(MixerPtr mixer, const void* src_buf, int32_t* accum_buf,
           bool accumulate, int32_t num_frames,
           Gain::AScale mix_scale = Gain::kUnityScale);

}  // namespace test
}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_SERVER_MIXER_TEST_MIXER_TESTS_SHARED_H_

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/shared/mixer/test/mixer_tests_shared.h"

namespace media::audio::test {

// Convenience abbreviations within this source file to shorten names
using Resampler = ::media::audio::Mixer::Resampler;

//
// Subtest utility functions -- used by test functions; can ASSERT on their own.
//
// Find a suitable mixer for the provided format, channels and frame rates.
// In testing, we choose ratio-of-frame-rates and source_channels carefully, to
// trigger the selection of a specific mixer. Note: Mixers convert audio into
// our accumulation format (not the destination format), so we need not specify
// a dest_format. Actual frame rate values are unimportant, but inter-rate RATIO
// is VERY important: required SRC is the primary factor in Mix selection.
std::unique_ptr<Mixer> SelectMixer(fuchsia::media::AudioSampleFormat source_format,
                                   int32_t source_channels, int32_t source_frame_rate,
                                   int32_t dest_channels, int32_t dest_frame_rate,
                                   Resampler resampler) {
  if (resampler == Resampler::Default) {
    EXPECT_TRUE(false);
    FX_LOGS(ERROR) << "Test should specify the Resampler exactly";
    return nullptr;
  }

  fuchsia::media::AudioStreamType source_details;
  source_details.sample_format = source_format;
  source_details.channels = source_channels;
  source_details.frames_per_second = source_frame_rate;

  fuchsia::media::AudioStreamType dest_details;
  dest_details.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  dest_details.channels = dest_channels;
  dest_details.frames_per_second = dest_frame_rate;

  return Mixer::Select(source_details, dest_details, resampler);
}

// Just as Mixers convert audio into our accumulation format, OutputProducer
// objects exist to convert frames of audio from accumulation format into
// destination format. They perform no SRC, gain scaling or rechannelization, so
// frames_per_second is unimportant and num_channels is only needed so that they
// can calculate the size of a (multi-channel) audio frame.
std::unique_ptr<OutputProducer> SelectOutputProducer(fuchsia::media::AudioSampleFormat dest_format,
                                                     int32_t num_channels) {
  fuchsia::media::AudioStreamType dest_details;
  dest_details.sample_format = dest_format;
  dest_details.channels = num_channels;
  dest_details.frames_per_second = 48000;

  return OutputProducer::Select(dest_details);
}

// Use the supplied mixer to scale from source into accum buffers.  Assumes a
// specific buffer size, with no SRC, starting at the beginning of each buffer.
// By default, does not gain-scale or accumulate (both can be overridden).
void DoMix(Mixer* mixer, const void* source_buf, float* accum_buf, bool accumulate,
           int64_t num_frames, float gain_db) {
  ASSERT_NE(mixer, nullptr);

  int64_t dest_offset = 0;
  auto source_offset = Fixed(0);

  mixer->gain.SetSourceGain(gain_db);
  mixer->Mix(accum_buf, num_frames, &dest_offset, source_buf, num_frames, &source_offset,
             accumulate);

  EXPECT_EQ(dest_offset, num_frames);
  EXPECT_EQ(source_offset, Fixed(dest_offset));
}

std::pair<double, double> SampleFormatToAmplitudes(fuchsia::media::AudioSampleFormat f) {
  double format_amplitude, double_amplitude;

  // Why isn't double_amplitude 1.0?  int16 and int8 have more negative values
  // than positive ones. To be linear without clipping, a full-scale signal
  // reaches the max (such as 0x7FFF) but not the min (such as -0x8000). Thus,
  // this magnitude is slightly less than the 1.0 we expect for float signals.
  switch (f) {
    case fuchsia::media::AudioSampleFormat::UNSIGNED_8:
      format_amplitude = kFullScaleInt8InputAmplitude;
      double_amplitude = kFullScaleInt8AccumAmplitude;
      break;
    case fuchsia::media::AudioSampleFormat::SIGNED_16:
      format_amplitude = kFullScaleInt16InputAmplitude;
      double_amplitude = kFullScaleInt16AccumAmplitude;
      break;
    case fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32:
      format_amplitude = kFullScaleInt24In32InputAmplitude;
      double_amplitude = kFullScaleInt24In32AccumAmplitude;
      break;
    case fuchsia::media::AudioSampleFormat::FLOAT:
      format_amplitude = kFullScaleFloatInputAmplitude;
      double_amplitude = kFullScaleFloatAccumAmplitude;
      break;
    default:
      FX_CHECK(false) << "Unsupported source format";
  }

  return std::make_pair(format_amplitude, double_amplitude);
}

}  // namespace media::audio::test

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/mixer/test/mixer_tests_shared.h"

namespace media {
namespace audio {
namespace test {

// Convenience abbreviations within this source file to shorten names
using Resampler = ::media::audio::Mixer::Resampler;

//
// Subtest utility functions -- used by test functions; can ASSERT on their own.
//
// Find a suitable mixer for the provided format, channels and frame rates.
// In testing, we choose ratio-of-frame-rates and src_channels carefully, to
// trigger the selection of a specific mixer. Note: Mixers convert audio into
// our accumulation format (not the destination format), so we need not specify
// a dst_format. Actual frame rate values are unimportant, but inter-rate RATIO
// is VERY important: required SRC is the primary factor in Mix selection.
MixerPtr SelectMixer(fuchsia::media::AudioSampleFormat src_format,
                     uint32_t src_channels, uint32_t src_frame_rate,
                     uint32_t dst_channels, uint32_t dst_frame_rate,
                     Resampler resampler) {
  fuchsia::media::AudioStreamType src_details;
  src_details.sample_format = src_format;
  src_details.channels = src_channels;
  src_details.frames_per_second = src_frame_rate;

  fuchsia::media::AudioStreamType dst_details;
  dst_details.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  dst_details.channels = dst_channels;
  dst_details.frames_per_second = dst_frame_rate;

  MixerPtr mixer = Mixer::Select(src_details, dst_details, resampler);

  return mixer;
}

// Just as Mixers convert audio into our accumulation format, OutputProducer
// objects exist to convert frames of audio from accumulation format into
// destination format. They perform no SRC, gain scaling or rechannelization, so
// frames_per_second is unimportant and num_channels is only needed so that they
// can calculate the size of a (multi-channel) audio frame.
OutputProducerPtr SelectOutputProducer(
    fuchsia::media::AudioSampleFormat dst_format, uint32_t num_channels) {
  fuchsia::media::AudioStreamTypePtr dst_details =
      fuchsia::media::AudioStreamType::New();
  dst_details->sample_format = dst_format;
  dst_details->channels = num_channels;
  dst_details->frames_per_second = 48000;

  OutputProducerPtr output_producer = OutputProducer::Select(dst_details);

  return output_producer;
}

// This shared function normalizes data arrays into our float32 pipeline.
// Because inputs must be in the range of [-2^27 , 2^27 ], for all practical
// purposes it wants "int28" inputs, hence this function's unexpected name. The
// test-data-width of 28 bits was chosen to accomodate float32 precision.
constexpr float kInt28ToFloat = 1.0 / (1 << 27);  // Why 27? Remember sign bit.
void NormalizeInt28ToPipelineBitwidth(float* source, uint32_t source_len) {
  for (uint32_t idx = 0; idx < source_len; ++idx) {
    source[idx] *= kInt28ToFloat;
  }
}

// Use the supplied mixer to scale from src into accum buffers.  Assumes a
// specific buffer size, with no SRC, starting at the beginning of each buffer.
// By default, does not gain-scale or accumulate (both can be overridden).
void DoMix(MixerPtr mixer, const void* src_buf, float* accum_buf,
           bool accumulate, int32_t num_frames, Gain::AScale mix_scale) {
  uint32_t dst_offset = 0;
  int32_t frac_src_offset = 0;
  bool mix_result =
      mixer->Mix(accum_buf, num_frames, &dst_offset, src_buf,
                 num_frames << kPtsFractionalBits, &frac_src_offset,
                 Mixer::FRAC_ONE, mix_scale, accumulate);

  EXPECT_TRUE(mix_result);
  EXPECT_EQ(static_cast<uint32_t>(num_frames), dst_offset);
  EXPECT_EQ(dst_offset << kPtsFractionalBits,
            static_cast<uint32_t>(frac_src_offset));
}

}  // namespace test
}  // namespace audio
}  // namespace media

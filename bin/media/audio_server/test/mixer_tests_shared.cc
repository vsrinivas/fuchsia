// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mixer_tests_shared.h"

namespace media {
namespace test {

using ASF = AudioSampleFormat;

//
// Subtest utility functions -- used by test functions; can ASSERT on their own.
//
// Find a suitable mixer for the provided format, channels and frame rates.
// In testing, we choose ratio-of-frame-rates and src_channels carefully, to
// trigger the selection of specific mixers. Note: Mixers convert audio into our
// accumulation format (not the destination format), so we need not specify a
// dst_format. Actual frame rate values are unimportant, but inter-rate RATIO
// is VERY important: required SRC is the primary factor in Mix selection.
// TODO(mpuryear): MTWN-90 Augment Mixer::Select, to specify which resampler --
// then come back and rework not only this helper function, but numerous tests
audio::MixerPtr SelectMixer(ASF src_format,
                            uint32_t src_channels,
                            uint32_t src_frame_rate,
                            uint32_t dst_channels,
                            uint32_t dst_frame_rate) {
  AudioMediaTypeDetailsPtr src_details = AudioMediaTypeDetails::New();
  src_details->sample_format = src_format;
  src_details->channels = src_channels;
  src_details->frames_per_second = src_frame_rate;

  AudioMediaTypeDetailsPtr dst_details = AudioMediaTypeDetails::New();
  dst_details->sample_format = ASF::SIGNED_16;
  dst_details->channels = dst_channels;
  dst_details->frames_per_second = dst_frame_rate;

  audio::MixerPtr mixer = audio::Mixer::Select(src_details, &dst_details);

  return mixer;
}

// Just as Mixers convert audio into our accumulation format, OutputFormatter
// objects exist to convert frames of audio from accumulation format into
// destination format. They perform no SRC, gain scaling or rechannelization, so
// frames_per_second is unimportant and num_channels is only needed so that they
// can calculate the size of a (multi-channel) audio frame.
audio::OutputFormatterPtr SelectOutputFormatter(ASF dst_format,
                                                uint32_t num_channels) {
  AudioMediaTypeDetailsPtr dst_details = AudioMediaTypeDetails::New();
  dst_details->sample_format = dst_format;
  dst_details->channels = num_channels;
  dst_details->frames_per_second = 1000;

  audio::OutputFormatterPtr output_formatter =
      audio::OutputFormatter::Select(dst_details);

  return output_formatter;
}

// Use the supplied mixer to scale from src into accum buffers.  Assumes a
// specific buffer size, with no SRC, starting at the beginning of each buffer.
// By default, does not gain-scale or accumulate (both can be overridden).
void DoMix(audio::MixerPtr mixer,
           const void* src_buf,
           int32_t* accum_buf,
           bool accumulate,
           int32_t num_frames,
           audio::Gain::AScale mix_scale) {
  uint32_t dst_offset = 0;
  int32_t frac_src_offset = 0;
  bool mix_result =
      mixer->Mix(accum_buf, num_frames, &dst_offset, src_buf,
                 num_frames << audio::kPtsFractionalBits, &frac_src_offset,
                 media::audio::Mixer::FRAC_ONE, mix_scale, accumulate);

  EXPECT_TRUE(mix_result);
  EXPECT_EQ(static_cast<uint32_t>(num_frames), dst_offset);
  EXPECT_EQ(dst_offset << audio::kPtsFractionalBits,
            static_cast<uint32_t>(frac_src_offset));
}

}  // namespace test
}  // namespace media

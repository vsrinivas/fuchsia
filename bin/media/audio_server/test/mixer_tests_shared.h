// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/audio_server/gain.h"
#include "garnet/bin/media/audio_server/platform/generic/mixer.h"
#include "garnet/bin/media/audio_server/platform/generic/output_formatter.h"
#include "gtest/gtest.h"

namespace media {
namespace test {

//
// Subtest shared helper functions -- used by tests; can ASSERT on their own.
//

// Find a suitable mixer for the provided format, channels and frame rates.
// TODO(mpuryear): add the ability to specify which resampler.
audio::MixerPtr SelectMixer(AudioSampleFormat src_format,
                            uint32_t src_channels,
                            uint32_t src_frame_rate,
                            uint32_t dst_channels,
                            uint32_t dst_frame_rate);

// OutputFormatters convert frames from accumulation format to dest format.
audio::OutputFormatterPtr SelectOutputFormatter(AudioSampleFormat dst_format,
                                                uint32_t num_channels);

// Use supplied mixer to mix (w/out rate conversion) from source to accumulator.
// TODO(mpuryear): refactor this so that tests just call mixer->Mix directly.
void DoMix(audio::MixerPtr mixer,
           const void* src_buf,
           int32_t* accum_buf,
           bool accumulate,
           int32_t num_frames,
           audio::Gain::AScale mix_scale = audio::Gain::kUnityScale);

}  // namespace test
}  // namespace media
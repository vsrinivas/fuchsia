// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/media/audio_server/mixer/test/frequency_set.h"
#include "garnet/bin/media/audio_server/mixer/test/mixer_tests_shared.h"

namespace media {
namespace audio {
namespace test {

class AudioPerformance {
 public:
  // class is static only - prevent attempts to instantiate it
  AudioPerformance() = delete;

  // The subsequent methods are used when profiling the performance of the core
  // Mix() function. They display the nanoseconds required to mix a buffer of
  // 64k samples, in various configurations. Results are displayed in an
  // easily-imported format. Use the --profile flag to trigger this.
  static void Profile();

 private:
  static void ProfileSampler(Mixer::Resampler sampler_type);
  static void ProfileSamplerIn(uint32_t in_chans,
                               Mixer::Resampler sampler_type);
  static void ProfileSamplerChans(uint32_t in_chans, uint32_t out_chans,
                                  Mixer::Resampler sampler_type);
  static void ProfileSamplerChansRate(uint32_t in_chans, uint32_t out_chans,
                                      Mixer::Resampler sampler_type,
                                      uint32_t source_rate);
  static void ProfileSamplerChansRateScale(uint32_t in_chans,
                                           uint32_t out_chans,
                                           Mixer::Resampler sampler_type,
                                           uint32_t source_rate,
                                           Gain::AScale gain_scale);
  static void ProfileSamplerChansRateScaleMix(uint32_t num_input_chans,
                                              uint32_t num_output_chans,
                                              Mixer::Resampler sampler_type,
                                              uint32_t source_rate,
                                              Gain::AScale gain_scale,
                                              bool accumulate);
  template <typename SampleType>
  static void ProfileMixer(uint32_t num_input_chans, uint32_t num_output_chans,
                           Mixer::Resampler sampler_type, uint32_t source_rate,
                           Gain::AScale gain_scale, bool accumulate);

  static void DisplayColumnHeader();
  static void DisplayConfigLegend();
};

}  // namespace test
}  // namespace audio
}  // namespace media

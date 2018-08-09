// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_AUDIO_CORE_MIXER_TEST_AUDIO_PERFORMANCE_H_
#define GARNET_BIN_MEDIA_AUDIO_CORE_MIXER_TEST_AUDIO_PERFORMANCE_H_

#include "garnet/bin/media/audio_core/mixer/test/frequency_set.h"
#include "garnet/bin/media/audio_core/mixer/test/mixer_tests_shared.h"

namespace media {
namespace audio {
namespace test {

enum class OutputDataRange {
  Silence = 0,
  OutOfRange,
  Normal,
};

class AudioPerformance {
 public:
  // After first run ("cold"), timings measured are tightly clustered (+/-1-2%);
  // we can get a high-confidence profile assessment with fewer runs.
  //
  // These values were chosen to keep Mixer and OutputProducer profile times
  // under 180 seconds each, on both a standard VIM2 and a standard NUC.
  static constexpr uint32_t kNumMixerProfilerRuns = 190;
  static constexpr uint32_t kNumOutputProfilerRuns = 2100;

  // class is static only - prevent attempts to instantiate it
  AudioPerformance() = delete;

  // The subsequent methods are used when profiling the performance of the core
  // Mix() function. They display the nanoseconds required to mix a buffer of
  // 64k samples, in various configurations. Results are displayed in an
  // easily-imported format. Use the --profile flag to trigger this.
  static void Profile();

 private:
  static void ProfileMixers();

  static void DisplayMixerColumnHeader();
  static void DisplayMixerConfigLegend();

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

  static void ProfileOutputProducers();

  static void DisplayOutputColumnHeader();
  static void DisplayOutputConfigLegend();

  static void ProfileOutputChans(uint32_t num_chans);
  static void ProfileOutputRange(uint32_t num_chans,
                                 OutputDataRange data_range);
  template <typename SampleType>
  static void ProfileOutputType(uint32_t num_chans, OutputDataRange data_range);
};

}  // namespace test
}  // namespace audio
}  // namespace media

#endif  // GARNET_BIN_MEDIA_AUDIO_CORE_MIXER_TEST_AUDIO_PERFORMANCE_H_

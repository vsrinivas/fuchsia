// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_TEST_AUDIO_PERFORMANCE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_TEST_AUDIO_PERFORMANCE_H_

#include "src/media/audio/audio_core/mixer/test/frequency_set.h"
#include "src/media/audio/audio_core/mixer/test/mixer_tests_shared.h"

namespace media::audio::test {

enum GainType : uint32_t { Mute, Unity, Scaled, Ramped };
enum OutputDataRange : uint32_t { Silence, OutOfRange, Normal };

// TODO(fxbug.dev/50811): Consider migrating to google/benchmark

class AudioPerformance {
 public:
  // After first run ("cold"), timings measured are tightly clustered (+/-1-2%);
  // we can get a high-confidence profile assessment with fewer runs.
  //
  // We set these values to keep Mixer profile times, and OutputProducer profile times, each at or
  // below 180 seconds, for a Release core build running on standard VIM2 and NUC.
  static constexpr uint32_t kNumMixerCreationRuns = 10;
  static constexpr uint32_t kNumMixerProfilerRuns = 10;
  static constexpr uint32_t kNumOutputProfilerRuns = 1500;

  // class is static only - prevent attempts to instantiate it
  AudioPerformance() = delete;

  // The subsequent methods are used when profiling the performance of the core
  // Mix() function. They display the nanoseconds required to mix a buffer of
  // 64k samples, in various configurations. Results are displayed in an
  // easily-imported format. Use the --profile flag to trigger this.
  static void Profile();

 private:
  static void ProfileMixers();

  static void DisplayMixerCreationLegend();
  static void DisplayMixerCreationColumnHeader();
  static void ProfileMixerCreation();
  static void ProfileMixerCreationType(Mixer::Resampler sampler_type);
  static void ProfileMixerCreationTypeChan(Mixer::Resampler sampler_type, uint32_t num_input_chans,
                                           uint32_t num_output_chans);
  static void ProfileMixerCreationTypeChanFormat(Mixer::Resampler sampler_type,
                                                 uint32_t num_input_chans,
                                                 uint32_t num_output_chans,
                                                 fuchsia::media::AudioSampleFormat sample_format);
  static void ProfileMixerCreationTypeChanFormatRate(
      Mixer::Resampler sampler_type, uint32_t num_input_chans, uint32_t num_output_chans,
      fuchsia::media::AudioSampleFormat sample_format, uint32_t source_rate, uint32_t dest_rate);

  static void DisplayMixerColumnHeader();
  static void DisplayMixerConfigLegend();

  static void ProfileSampler(Mixer::Resampler sampler_type);
  static void ProfileSamplerIn(uint32_t in_chans, Mixer::Resampler sampler_type);
  static void ProfileSamplerChans(uint32_t in_chans, uint32_t out_chans,
                                  Mixer::Resampler sampler_type);
  static void ProfileSamplerChansRate(uint32_t in_chans, uint32_t out_chans,
                                      Mixer::Resampler sampler_type, uint32_t source_rate);
  static void ProfileSamplerChansRateScale(uint32_t in_chans, uint32_t out_chans,
                                           Mixer::Resampler sampler_type, uint32_t source_rate,
                                           GainType gain_type);
  static void ProfileSamplerChansRateScaleMix(uint32_t num_input_chans, uint32_t num_output_chans,
                                              Mixer::Resampler sampler_type, uint32_t source_rate,
                                              GainType gain_type, bool accumulate);
  template <fuchsia::media::AudioSampleFormat SampleFormat>
  static void ProfileMixer(uint32_t num_input_chans, uint32_t num_output_chans,
                           Mixer::Resampler sampler_type, uint32_t source_rate, GainType gain_type,
                           bool accumulate);

  static void ProfileOutputProducers();

  static void DisplayOutputColumnHeader();
  static void DisplayOutputConfigLegend();

  static void ProfileOutputChans(uint32_t num_chans);
  static void ProfileOutputRange(uint32_t num_chans, OutputDataRange data_range);
  template <fuchsia::media::AudioSampleFormat SampleFormat>
  static void ProfileOutputType(uint32_t num_chans, OutputDataRange data_range);
};

}  // namespace media::audio::test

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_TEST_AUDIO_PERFORMANCE_H_

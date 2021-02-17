// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_TOOLS_AUDIO_PERFORMANCE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_TOOLS_AUDIO_PERFORMANCE_H_

#include "src/media/audio/audio_core/mixer/gain.h"
#include "src/media/audio/audio_core/mixer/mixer.h"
#include "src/media/audio/audio_core/mixer/output_producer.h"

namespace media::audio::tools {

enum class BenchmarkType { All, CreationOnly, MixingOnly, OutputOnly };
enum GainType : uint32_t { Mute, Unity, Scaled, Ramped };
enum OutputDataRange : uint32_t { Silence, OutOfRange, Normal };

// TODO(fxbug.dev/50811): Consider migrating to google/benchmark

// The AudioPerformance class profiles the performance of the Mixer, Gain and OutputProducer
// classes. These micro-benchmark tests use *zx::clock::get_monotonic()* to measure the time
// required for a Mixer/Gain or OutputProducer to execute Mix() or ProduceOutput() respectively,
// generating 64k output frames. It also profiles the time required for initial mixer creation.
//
// The aggregated results displayed for each permutation of parameters represent the time consumed
// *per-call* or *per-creation*, although to determine a relatively reliable Mean we run the
// micro-benchmarks many tens or even hundreds of times.
//
// As is often the case with performance profiling, one generally should not directly compare
// results from different machines; one would use profiling functionality primarily to gain a sense
// of "before versus after" with respect to a specific change affecting the mixer pipeline.
class AudioPerformance {
 public:
  // class is static only - prevent attempts to instantiate it
  AudioPerformance() = delete;

  static void SetBenchmarkCreationOnly() { benchmark_type_ = BenchmarkType::CreationOnly; }
  static void SetBenchmarkMixingOnly() { benchmark_type_ = BenchmarkType::MixingOnly; }
  static void SetBenchmarkOutputOnly() { benchmark_type_ = BenchmarkType::OutputOnly; }

  // Subsequent methods profile the performance of mixer creation, the core Mix() function, and the
  // final ProduceOutput() function. Each displays nanoseconds required, in various configurations.
  // Results are displayed in an easily-imported format.
  static int Profile();

 private:
  // After first run ("cold"), the timings measured are usually tightly clustered (+/-1-2%).
  //
  // We set these values to keep profile times reasonable: totalling no more than 5 minutes
  // (30/240/30 secs respectively) for a Release core build running on standard platforms.
  static constexpr uint32_t kNumMixerCreationRuns = 50;
  static constexpr uint32_t kNumMixerProfilerRuns = 29;
  static constexpr uint32_t kNumOutputProfilerRuns = 341;

  static const uint32_t kProfilerBufferSize;
  static const uint32_t kProfilerFrequency;

  static void ProfileMixerCreation();
  static void DisplayMixerCreationLegend();
  static void DisplayMixerCreationColumnHeader();
  static void ProfileMixerCreationType(Mixer::Resampler sampler_type);
  static void ProfileMixerCreationTypeChan(Mixer::Resampler sampler_type, uint32_t num_input_chans,
                                           uint32_t num_output_chans);
  static void ProfileMixerCreationTypeChanRate(Mixer::Resampler sampler_type,
                                               uint32_t num_input_chans, uint32_t num_output_chans,
                                               uint32_t source_rate, uint32_t dest_rate);
  static void ProfileMixerCreationTypeChanRateFormat(
      Mixer::Resampler sampler_type, uint32_t num_input_chans, uint32_t num_output_chans,
      uint32_t source_rate, uint32_t dest_rate, fuchsia::media::AudioSampleFormat sample_format);

  static void ProfileMixing();
  static void DisplayMixerConfigLegend();
  static void DisplayMixerColumnHeader();
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
  static void ProfileMix(uint32_t num_input_chans, uint32_t num_output_chans,
                         Mixer::Resampler sampler_type, uint32_t source_rate, GainType gain_type,
                         bool accumulate);

  static void ProfileOutputProducers();
  static void DisplayOutputConfigLegend();
  static void DisplayOutputColumnHeader();
  static void ProfileOutputChans(uint32_t num_chans);
  static void ProfileOutputRange(uint32_t num_chans, OutputDataRange data_range);
  template <fuchsia::media::AudioSampleFormat SampleFormat>
  static void ProfileOutputType(uint32_t num_chans, OutputDataRange data_range);

  static BenchmarkType benchmark_type_;
};

}  // namespace media::audio::tools

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_TOOLS_AUDIO_PERFORMANCE_H_

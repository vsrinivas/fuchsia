// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_TOOLS_AUDIO_PERFORMANCE_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_TOOLS_AUDIO_PERFORMANCE_H_

#include "src/media/audio/audio_core/mixer/gain.h"
#include "src/media/audio/audio_core/mixer/mixer.h"
#include "src/media/audio/audio_core/mixer/output_producer.h"

namespace media::audio::tools {

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

  enum class GainType { Mute, Unity, Scaled, Ramped };
  enum class InputRange { Silence, OutOfRange, Normal };

  struct MixerConfig {
    Mixer::Resampler sampler_type;
    uint32_t num_input_chans;
    uint32_t num_output_chans;
    uint32_t source_rate;
    uint32_t dest_rate;
    fuchsia::media::AudioSampleFormat sample_format;
    GainType gain_type;  // ProfileMixing() only
    bool accumulate;     // ProfileMixing() only

    std::string ToStringForCreate() const;
    std::string ToStringForMixer() const;
  };

  struct OutputProducerConfig {
    fuchsia::media::AudioSampleFormat sample_format;
    InputRange input_range;
    uint32_t num_chans;

    std::string ToString() const;
  };

  static void ProfileMixerCreation(const std::vector<MixerConfig>& configs,
                                   zx::duration duration_per_config);
  static void ProfileMixing(const std::vector<MixerConfig>& configs,
                            zx::duration duration_per_config);
  static void ProfileOutputProducer(const std::vector<OutputProducerConfig>& configs,
                                    zx::duration duration_per_config);

 private:
  static void DisplayMixerCreationLegend();
  static void DisplayMixerCreationColumnHeader();
  static void ProfileMixerCreation(const MixerConfig& cfg, zx::duration total_duration);

  static void DisplayMixerLegend();
  static void DisplayMixerColumnHeader();
  template <fuchsia::media::AudioSampleFormat SampleFormat>
  static void ProfileMixing(const MixerConfig& cfg, zx::duration total_duration);

  static void DisplayOutputConfigLegend();
  static void DisplayOutputColumnHeader();
  template <fuchsia::media::AudioSampleFormat SampleFormat>
  static void ProfileOutputProducer(const OutputProducerConfig& cfg, zx::duration total_duration);
};

}  // namespace media::audio::tools

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_MIXER_TOOLS_AUDIO_PERFORMANCE_H_

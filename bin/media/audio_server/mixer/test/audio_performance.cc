// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/mixer/test/audio_performance.h"
#include "garnet/bin/media/audio_server/mixer/test/frequency_set.h"
#include "garnet/bin/media/audio_server/mixer/test/mixer_tests_shared.h"

namespace media {
namespace audio {
namespace test {

constexpr uint32_t kNumProfilerRuns = 100;

// Convenience abbreviation within this source file to shorten names
using Resampler = media::audio::Mixer::Resampler;

void AudioPerformance::DisplayColumnHeader() {
  printf("Configuration\t    Mean\t   First\t    Best\t   Worst\n");
}

void AudioPerformance::DisplayConfigLegend() {
  printf("\n   Elapsed time in microsec for Mix() to produce %u frames\n",
         kFreqTestBufSize);
  printf(
      "\n   For mixer configuration Rf.IOGAnnnnn, where:\n"
      "\t    R: Resampler type [Linear, Point],\n"
      "\t    f: source Format [uint8, int16, float],\n"
      "\t    I: Input channels [#],\n"
      "\t    O: Output channels [#],\n"
      "\t    G: Gain factor [Unity, Scaled, Mute],\n"
      "\t    A: Accumulate [yes(+), no(-)],\n"
      "\tnnnnn: source sample rate [#]\n\n");
}

// For the given resampler, measure elapsed time over a number of mix jobs.
void AudioPerformance::Profile() {
  printf("\n\n Performance Profiling");

  AudioPerformance::DisplayConfigLegend();
  AudioPerformance::DisplayColumnHeader();

  AudioPerformance::ProfileSampler(Resampler::SampleAndHold);
  AudioPerformance::ProfileSampler(Resampler::LinearInterpolation);

  AudioPerformance::DisplayColumnHeader();
  AudioPerformance::DisplayConfigLegend();
}

void AudioPerformance::ProfileSampler(Resampler sampler_type) {
  ProfileSamplerIn(1, sampler_type);
  ProfileSamplerIn(2, sampler_type);
  ProfileSamplerIn(3, sampler_type);
  ProfileSamplerIn(4, sampler_type);
}

void AudioPerformance::ProfileSamplerIn(uint32_t num_input_chans,
                                        Resampler sampler_type) {
  if (num_input_chans > 2) {
    ProfileSamplerChans(num_input_chans, num_input_chans, sampler_type);

  } else {
    ProfileSamplerChans(num_input_chans, 1, sampler_type);
    ProfileSamplerChans(num_input_chans, 2, sampler_type);
  }
}

void AudioPerformance::ProfileSamplerChans(uint32_t num_input_chans,
                                           uint32_t num_output_chans,
                                           Resampler sampler_type) {
  ProfileSamplerChansRate(num_input_chans, num_output_chans, sampler_type,
                          48000);
  ProfileSamplerChansRate(num_input_chans, num_output_chans, sampler_type,
                          44100);
}

void AudioPerformance::ProfileSamplerChansRate(uint32_t num_input_chans,
                                               uint32_t num_output_chans,
                                               Resampler sampler_type,
                                               uint32_t source_rate) {
  ProfileSamplerChansRateScale(num_input_chans, num_output_chans, sampler_type,
                               source_rate, 0);
  ProfileSamplerChansRateScale(num_input_chans, num_output_chans, sampler_type,
                               source_rate, Gain::kUnityScale);
  ProfileSamplerChansRateScale(num_input_chans, num_output_chans, sampler_type,
                               source_rate, Gain::kMaxScale);
}

void AudioPerformance::ProfileSamplerChansRateScale(uint32_t num_input_chans,
                                                    uint32_t num_output_chans,
                                                    Resampler sampler_type,
                                                    uint32_t source_rate,
                                                    Gain::AScale gain_scale) {
  ProfileSamplerChansRateScaleMix(num_input_chans, num_output_chans,
                                  sampler_type, source_rate, gain_scale, false);
  ProfileSamplerChansRateScaleMix(num_input_chans, num_output_chans,
                                  sampler_type, source_rate, gain_scale, true);
}

void AudioPerformance::ProfileSamplerChansRateScaleMix(
    uint32_t num_input_chans, uint32_t num_output_chans, Resampler sampler_type,
    uint32_t source_rate, Gain::AScale gain_scale, bool accumulate) {
  ProfileMixer<uint8_t>(num_input_chans, num_output_chans, sampler_type,
                        source_rate, gain_scale, accumulate);
  ProfileMixer<int16_t>(num_input_chans, num_output_chans, sampler_type,
                        source_rate, gain_scale, accumulate);
  ProfileMixer<float>(num_input_chans, num_output_chans, sampler_type,
                      source_rate, gain_scale, accumulate);
}

template <typename SampleType>
void AudioPerformance::ProfileMixer(uint32_t num_input_chans,
                                    uint32_t num_output_chans,
                                    Resampler sampler_type,
                                    uint32_t source_rate,
                                    Gain::AScale gain_scale, bool accumulate) {
  AudioSampleFormat sample_format;
  double amplitude;
  char format;
  if (std::is_same<SampleType, uint8_t>::value) {
    sample_format = AudioSampleFormat::UNSIGNED_8;
    amplitude = std::numeric_limits<int8_t>::max();
    format = 'u';
  } else if (std::is_same<SampleType, int16_t>::value) {
    sample_format = AudioSampleFormat::SIGNED_16;
    amplitude = std::numeric_limits<int16_t>::max();
    format = 'i';
  } else if (std::is_same<SampleType, float>::value) {
    sample_format = AudioSampleFormat::FLOAT;
    amplitude = 1.0;
    format = 'f';
  } else {
    ASSERT_TRUE(false) << "Unknown mix sample format for testing";
    return;
  }

  uint32_t dest_rate = 48000;
  MixerPtr mixer = SelectMixer(sample_format, num_input_chans, source_rate,
                               num_output_chans, dest_rate, sampler_type);

  uint32_t source_buffer_size = kFreqTestBufSize * dest_rate / source_rate;
  uint32_t source_frames = source_buffer_size + 1;
  uint32_t frac_step_size = (source_rate * Mixer::FRAC_ONE) / dest_rate;
  uint32_t modulo =
      (source_rate * Mixer::FRAC_ONE) - (frac_step_size * dest_rate);

  std::vector<SampleType> source(source_frames * num_input_chans);
  std::vector<int32_t> accum(kFreqTestBufSize * num_output_chans);
  uint32_t frac_src_frames = source_frames * Mixer::FRAC_ONE;
  int32_t frac_src_offset;
  uint32_t dst_offset;

  OverwriteCosine(source.data(), source_buffer_size * num_input_chans,
                  FrequencySet::kReferenceFreqs[FrequencySet::kRefFreqIdx],
                  amplitude);

  zx_duration_t first, worst, best, total_elapsed = 0;
  for (uint32_t i = 0; i < kNumProfilerRuns; ++i) {
    zx_duration_t elapsed;
    zx_time_t start_time = zx_clock_get(ZX_CLOCK_MONOTONIC);

    dst_offset = 0;
    frac_src_offset = 0;
    mixer->Mix(accum.data(), kFreqTestBufSize, &dst_offset, source.data(),
               frac_src_frames, &frac_src_offset, frac_step_size, gain_scale,
               accumulate, modulo, dest_rate);

    elapsed = zx_clock_get(ZX_CLOCK_MONOTONIC) - start_time;

    if (i > 0) {
      worst = std::max(worst, elapsed);
      best = std::min(best, elapsed);
    } else {
      first = elapsed;
      worst = elapsed;
      best = elapsed;
    }
    total_elapsed += elapsed;
  }

  double mean = total_elapsed / kNumProfilerRuns;
  printf("%c%c.%u%u%c%c%u:",
         (sampler_type == Resampler::LinearInterpolation ? 'L' : 'P'), format,
         num_input_chans, num_output_chans,
         (gain_scale ? (gain_scale == Gain::kUnityScale ? 'U' : 'S') : 'M'),
         (accumulate ? '+' : '-'), source_rate);

  printf("\t%9.3lf\t%9.3lf\t%9.3lf\t%9.3lf\n", mean / 1000.0, first / 1000.0,
         best / 1000.0, worst / 1000.0);
}

}  // namespace test
}  // namespace audio
}  // namespace media

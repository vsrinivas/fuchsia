// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_server/test/audio_performance.h"
#include "garnet/bin/media/audio_server/test/frequency_set.h"
#include "garnet/bin/media/audio_server/test/mixer_tests_shared.h"

namespace media {
namespace audio {
namespace test {

constexpr uint32_t kNumProfilerRuns = 1000;

// Convenience abbreviation within this source file to shorten names
using Resampler = media::audio::Mixer::Resampler;

// For the given resampler, measure elapsed time over a number of mix jobs.
void AudioPerformance::Profile() {
  printf("\n");

  AudioPerformance::ProfileSampler(Resampler::SampleAndHold);
  AudioPerformance::ProfileSampler(Resampler::LinearInterpolation);

  printf("\n");
}

void AudioPerformance::ProfileSampler(Resampler sampler_type) {
  ProfileSamplerIn(1, sampler_type);
  ProfileSamplerIn(2, sampler_type);
  ProfileSamplerIn(3, sampler_type);
  ProfileSamplerIn(4, sampler_type);
}

void AudioPerformance::ProfileSamplerIn(uint32_t in_chans,
                                        Resampler sampler_type) {
  if (in_chans > 2) {
    ProfileSamplerChans(in_chans, in_chans, sampler_type);

  } else {
    ProfileSamplerChans(in_chans, 1, sampler_type);
    ProfileSamplerChans(in_chans, 2, sampler_type);
  }
}

void AudioPerformance::ProfileSamplerChans(uint32_t in_chans,
                                           uint32_t out_chans,
                                           Resampler sampler_type) {
  ProfileSamplerChansRate(in_chans, out_chans, sampler_type, 48000);
  ProfileSamplerChansRate(in_chans, out_chans, sampler_type, 44100);
}

void AudioPerformance::ProfileSamplerChansRate(uint32_t in_chans,
                                               uint32_t out_chans,
                                               Resampler sampler_type,
                                               uint32_t source_rate) {
  ProfileSamplerChansRateScale(in_chans, out_chans, sampler_type, source_rate,
                               0);
  ProfileSamplerChansRateScale(in_chans, out_chans, sampler_type, source_rate,
                               Gain::kUnityScale);
  ProfileSamplerChansRateScale(in_chans, out_chans, sampler_type, source_rate,
                               Gain::kMaxScale);
}

void AudioPerformance::ProfileSamplerChansRateScale(uint32_t in_chans,
                                                    uint32_t out_chans,
                                                    Resampler sampler_type,
                                                    uint32_t source_rate,
                                                    Gain::AScale gain_scale) {
  ProfileMixer(in_chans, out_chans, sampler_type, source_rate, gain_scale,
               false);
  ProfileMixer(in_chans, out_chans, sampler_type, source_rate, gain_scale,
               true);
}

void AudioPerformance::ProfileMixer(uint32_t num_input_chans,
                                    uint32_t num_output_chans,
                                    Resampler sampler_type,
                                    uint32_t source_rate,
                                    Gain::AScale gain_scale, bool accumulate) {
  uint32_t dest_rate = 48000;
  MixerPtr mixer =
      SelectMixer(AudioSampleFormat::FLOAT, num_input_chans, source_rate,
                  num_output_chans, dest_rate, sampler_type);

  uint32_t source_buffer_size = kFreqTestBufSize * dest_rate / source_rate;
  uint32_t source_frames = source_buffer_size + 1;
  uint32_t frac_step_size = (source_rate * Mixer::FRAC_ONE) / dest_rate;
  uint32_t modulo =
      (source_rate * Mixer::FRAC_ONE) - (frac_step_size * dest_rate);

  std::vector<float> source(source_frames * num_input_chans);
  std::vector<int32_t> accum(kFreqTestBufSize * num_output_chans);
  uint32_t frac_src_frames = source_frames * Mixer::FRAC_ONE;
  int32_t frac_src_offset;
  uint32_t dst_offset;

  OverwriteCosine(source.data(), source_buffer_size * num_input_chans,
                  FrequencySet::kReferenceFreqs[FrequencySet::kRefFreqIdx]);

  zx_duration_t elapsed;
  zx_time_t start_time = zx_clock_get(ZX_CLOCK_MONOTONIC);
  for (uint32_t i = 0; i < kNumProfilerRuns; ++i) {
    dst_offset = 0;
    frac_src_offset = 0;
    mixer->Mix(accum.data(), kFreqTestBufSize, &dst_offset, source.data(),
               frac_src_frames, &frac_src_offset, frac_step_size, gain_scale,
               accumulate, modulo, dest_rate);
  }

  elapsed = zx_clock_get(ZX_CLOCK_MONOTONIC) - start_time;
  printf("%c%u%u%c%c%u:\t%7.3f",
         (sampler_type == Resampler::LinearInterpolation ? 'L' : 'P'),
         num_input_chans, num_output_chans,
         (gain_scale ? (gain_scale == Gain::kUnityScale ? 'U' : 'G') : 'X'),
         (accumulate ? '+' : '-'), source_rate, elapsed / 1000000.0f);
  printf("\n");
}

}  // namespace test
}  // namespace audio
}  // namespace media

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/mixer/test/audio_performance.h"
#include "garnet/bin/media/audio_core/mixer/test/frequency_set.h"
#include "garnet/bin/media/audio_core/mixer/test/mixer_tests_shared.h"

namespace media {
namespace audio {
namespace test {

// Convenience abbreviation within this source file to shorten names
using Resampler = ::media::audio::Mixer::Resampler;

// For the given resampler, measure elapsed time over a number of mix jobs.
void AudioPerformance::Profile() {
  printf("\n\n Performance Profiling");

  AudioPerformance::ProfileMixers();
  AudioPerformance::ProfileOutputFormatters();
}

void AudioPerformance::ProfileMixers() {
  zx_time_t start_time = zx_clock_get(ZX_CLOCK_MONOTONIC);

  DisplayMixerConfigLegend();
  DisplayMixerColumnHeader();

  ProfileSampler(Resampler::SampleAndHold);
  ProfileSampler(Resampler::LinearInterpolation);

  DisplayMixerColumnHeader();
  DisplayMixerConfigLegend();

  printf("   Total time to profile Mixers: %lu ms\n   --------\n\n",
         (zx_clock_get(ZX_CLOCK_MONOTONIC) - start_time) / 1000000);
}

void AudioPerformance::DisplayMixerColumnHeader() {
  printf("Configuration\t    Mean\t   First\t    Best\t   Worst\n");
}

void AudioPerformance::DisplayMixerConfigLegend() {
  printf("\n   Elapsed time in microsec for Mix() to produce %u frames\n",
         kFreqTestBufSize);
  printf(
      "\n   For mixer configuration Rf.IOGAnnnnn, where:\n"
      "\t    R: Resampler type - [P]oint, [L]inear\n"
      "\t    f: source Format - [u]int8, [i]nt16, [f]loat,\n"
      "\t    I: Input channels (one-digit number),\n"
      "\t    O: Output channels (one-digit number),\n"
      "\t    G: Gain factor - [M]ute, [U]nity, [S]caled,\n"
      "\t    A: Accumulate - [-] no or [+] yes,\n"
      "\tnnnnn: source sample rate (five-digit number)\n\n");
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
  fuchsia::media::AudioSampleFormat sample_format;
  double amplitude;
  char format;
  if (std::is_same<SampleType, uint8_t>::value) {
    sample_format = fuchsia::media::AudioSampleFormat::UNSIGNED_8;
    amplitude = std::numeric_limits<int8_t>::max();
    format = 'u';
  } else if (std::is_same<SampleType, int16_t>::value) {
    sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
    amplitude = std::numeric_limits<int16_t>::max();
    format = 'i';
  } else if (std::is_same<SampleType, float>::value) {
    sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
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

  std::unique_ptr<SampleType[]> source =
      std::make_unique<SampleType[]>(source_frames * num_input_chans);
  std::unique_ptr<float[]> accum =
      std::make_unique<float[]>(kFreqTestBufSize * num_output_chans);
  uint32_t frac_src_frames = source_frames * Mixer::FRAC_ONE;
  int32_t frac_src_offset;
  uint32_t dst_offset;

  OverwriteCosine(source.get(), source_buffer_size * num_input_chans,
                  FrequencySet::kReferenceFreqs[FrequencySet::kRefFreqIdx],
                  amplitude);

  zx_duration_t first, worst, best, total_elapsed = 0;
  for (uint32_t i = 0; i < kNumMixerProfilerRuns; ++i) {
    zx_duration_t elapsed;
    zx_time_t start_time = zx_clock_get(ZX_CLOCK_MONOTONIC);

    dst_offset = 0;
    frac_src_offset = 0;
    mixer->Mix(accum.get(), kFreqTestBufSize, &dst_offset, source.get(),
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

  double mean = total_elapsed / kNumMixerProfilerRuns;
  printf("%c%c.%u%u%c%c%u:",
         (sampler_type == Resampler::SampleAndHold ? 'P' : 'L'), format,
         num_input_chans, num_output_chans,
         (gain_scale ? (gain_scale == Gain::kUnityScale ? 'U' : 'S') : 'M'),
         (accumulate ? '+' : '-'), source_rate);

  printf("\t%9.3lf\t%9.3lf\t%9.3lf\t%9.3lf\n", mean / 1000.0, first / 1000.0,
         best / 1000.0, worst / 1000.0);
}

void AudioPerformance::DisplayOutputColumnHeader() {
  printf("Config\t    Mean\t   First\t    Best\t   Worst\n");
}

void AudioPerformance::DisplayOutputConfigLegend() {
  printf("\n   Elapsed time in microsec to ProduceOutput() %u frames\n",
         kFreqTestBufSize);
  printf(
      "\n   For output configuration FRn, where:\n"
      "\t    F: Format of source data - [U]int8, [I]nt16, [F]loat,\n"
      "\t    R: Range of source data - [S]ilence, [O]ut-of-range, [N]ormal,\n"
      "\t    n: Number of output channels (one-digit number)\n\n");
}

void AudioPerformance::ProfileOutputFormatters() {
  zx_time_t start_time = zx_clock_get(ZX_CLOCK_MONOTONIC);

  DisplayOutputConfigLegend();
  DisplayOutputColumnHeader();

  ProfileOutputChans(1);
  ProfileOutputChans(2);
  ProfileOutputChans(4);
  ProfileOutputChans(6);
  ProfileOutputChans(8);

  DisplayOutputColumnHeader();
  DisplayOutputConfigLegend();

  printf("   Total time to profile OutputFormatters: %lu ms\n   --------\n\n",
         (zx_clock_get(ZX_CLOCK_MONOTONIC) - start_time) / 1000000);
}

void AudioPerformance::ProfileOutputChans(uint32_t num_chans) {
  ProfileOutputRange(num_chans, OutputDataRange::Silence);
  ProfileOutputRange(num_chans, OutputDataRange::OutOfRange);
  ProfileOutputRange(num_chans, OutputDataRange::Normal);
}

void AudioPerformance::ProfileOutputRange(uint32_t num_chans,
                                          OutputDataRange data_range) {
  ProfileOutputType<uint8_t>(num_chans, data_range);
  ProfileOutputType<int16_t>(num_chans, data_range);
  ProfileOutputType<float>(num_chans, data_range);
}

template <typename SampleType>
void AudioPerformance::ProfileOutputType(uint32_t num_chans,
                                         OutputDataRange data_range) {
  fuchsia::media::AudioSampleFormat sample_format;
  char format, range;

  if (std::is_same<SampleType, uint8_t>::value) {
    sample_format = fuchsia::media::AudioSampleFormat::UNSIGNED_8;
    format = 'U';
  } else if (std::is_same<SampleType, int16_t>::value) {
    sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
    format = 'I';
  } else if (std::is_same<SampleType, float>::value) {
    sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
    format = 'F';
  } else {
    ASSERT_TRUE(false) << "Unknown output sample format for testing";
    return;
  }

  audio::OutputFormatterPtr output_formatter =
      SelectOutputFormatter(sample_format, num_chans);

  uint32_t num_samples = kFreqTestBufSize * num_chans;

  std::unique_ptr<float[]> accum = std::make_unique<float[]>(num_samples);
  std::unique_ptr<SampleType[]> dest =
      std::make_unique<SampleType[]>(num_samples);

  switch (data_range) {
    case OutputDataRange::Silence:
      range = 'S';
      break;
    case OutputDataRange::OutOfRange:
      range = 'O';
      for (size_t idx = 0; idx < num_samples; ++idx) {
        accum[idx] = (idx % 2 ? -1.5f : 1.5f);
      }
      break;
    case OutputDataRange::Normal:
      range = 'N';
      OverwriteCosine(accum.get(), num_samples,
                      FrequencySet::kReferenceFreqs[FrequencySet::kRefFreqIdx]);
      break;
    default:
      ASSERT_TRUE(false) << "Unknown output sample format for testing";
      return;
  }

  zx_duration_t first, worst, best, total_elapsed = 0;

  if (data_range == OutputDataRange::Silence) {
    for (uint32_t i = 0; i < kNumOutputProfilerRuns; ++i) {
      zx_duration_t elapsed;
      zx_time_t start_time = zx_clock_get(ZX_CLOCK_MONOTONIC);

      output_formatter->FillWithSilence(dest.get(), kFreqTestBufSize);
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
  } else {
    for (uint32_t i = 0; i < kNumOutputProfilerRuns; ++i) {
      zx_duration_t elapsed;
      zx_time_t start_time = zx_clock_get(ZX_CLOCK_MONOTONIC);

      output_formatter->ProduceOutput(accum.get(), dest.get(),
                                      kFreqTestBufSize);
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
  }

  double mean = total_elapsed / kNumOutputProfilerRuns;
  printf("%c%c%u:\t%9.3lf\t%9.3lf\t%9.3lf\t%9.3lf\n", format, range, num_chans,
         mean / 1000.0, first / 1000.0, best / 1000.0, worst / 1000.0);
}

}  // namespace test
}  // namespace audio
}  // namespace media

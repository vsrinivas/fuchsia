// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/test/audio_performance.h"

#include <lib/zx/clock.h>

#include <string>

#include "src/media/audio/audio_core/mixer/test/frequency_set.h"
#include "src/media/audio/audio_core/mixer/test/mixer_tests_shared.h"

namespace media::audio::test {

float to_frac_usecs(zx::duration duration) {
  return static_cast<double>(duration.to_nsecs()) / 1000.0;
}

// Convenience abbreviation within this source file to shorten names
using Resampler = ::media::audio::Mixer::Resampler;

// For the given resampler, measure elapsed time over a number of mix jobs.
void AudioPerformance::Profile() {
  printf("\n\n Performance Profiling");

  AudioPerformance::ProfileMixers();
  AudioPerformance::ProfileOutputProducers();
}

void AudioPerformance::ProfileMixers() {
  auto start_time = zx::clock::get_monotonic();

  DisplayMixerConfigLegend();
  DisplayMixerColumnHeader();

  ProfileSampler(Resampler::SampleAndHold);
  ProfileSampler(Resampler::LinearInterpolation);
  ProfileSampler(Resampler::WindowedSinc);

  DisplayMixerColumnHeader();
  DisplayMixerConfigLegend();

  printf("   Total time to profile Mixers: %lu ms\n   --------\n\n",
         (zx::clock::get_monotonic() - start_time).get() / ZX_MSEC(1));
}

void AudioPerformance::DisplayMixerColumnHeader() {
  printf("Configuration\t\t     Mean\t    First\t     Best\t    Worst\n");
}

void AudioPerformance::DisplayMixerConfigLegend() {
  printf("\n   Elapsed time in microsec for Mix() to produce %u frames\n", kFreqTestBufSize);
  printf(
      "\n   For mixer configuration R-fff.IOGAnnnnn, where:\n"
      "\t     R: Resampler type - [P]oint, [L]inear, [W]indowed Sinc\n"
      "\t   fff: Format - un8, i16, i24, f32,\n"
      "\t     I: Input channels (one-digit number),\n"
      "\t     O: Output channels (one-digit number),\n"
      "\t     G: Gain factor - [M]ute, [U]nity, [S]caled, [R]amping,\n"
      "\t     A: Accumulate - [-] no or [+] yes,\n"
      "\t nnnnn: Sample rate (five-digit number)\n\n");
}

// Profile the samplers in various input and output channel configurations
void AudioPerformance::ProfileSampler(Resampler sampler_type) {
  ProfileSamplerIn(1, sampler_type);
  ProfileSamplerIn(2, sampler_type);
  ProfileSamplerIn(4, sampler_type);
}

// Based on our lack of support for arbitrary channelization, only profile the following channel
// configurations: 1-1, 1-2, 2-1, 2-2, 4-4
void AudioPerformance::ProfileSamplerIn(uint32_t num_input_chans, Resampler sampler_type) {
  if (num_input_chans > 2) {
    ProfileSamplerChans(num_input_chans, num_input_chans, sampler_type);
  } else {
    ProfileSamplerChans(num_input_chans, 1, sampler_type);
    ProfileSamplerChans(num_input_chans, 2, sampler_type);
  }
}

// Profile the samplers in scenarios with, and without, frame rate conversion
void AudioPerformance::ProfileSamplerChans(uint32_t num_input_chans, uint32_t num_output_chans,
                                           Resampler sampler_type) {
  ProfileSamplerChansRate(num_input_chans, num_output_chans, sampler_type, 48000);
  ProfileSamplerChansRate(num_input_chans, num_output_chans, sampler_type, 44100);
}

// Profile the samplers with gains of: Mute, Unity, Scaling (non-mute non-unity)
void AudioPerformance::ProfileSamplerChansRate(uint32_t num_input_chans, uint32_t num_output_chans,
                                               Resampler sampler_type, uint32_t source_rate) {
  // Mute scenario
  ProfileSamplerChansRateScale(num_input_chans, num_output_chans, sampler_type, source_rate,
                               GainType::Mute);
  // Unity scenario
  ProfileSamplerChansRateScale(num_input_chans, num_output_chans, sampler_type, source_rate,
                               GainType::Unity);
  // Scaling (non-mute, non-unity) scenario
  ProfileSamplerChansRateScale(num_input_chans, num_output_chans, sampler_type, source_rate,
                               GainType::Scaled);
  // Ramping scenario
  ProfileSamplerChansRateScale(num_input_chans, num_output_chans, sampler_type, source_rate,
                               GainType::Ramped);
}

// Profile the samplers when not accumulating and when accumulating
void AudioPerformance::ProfileSamplerChansRateScale(uint32_t num_input_chans,
                                                    uint32_t num_output_chans,
                                                    Resampler sampler_type, uint32_t source_rate,
                                                    GainType gain_type) {
  // Overwrite any previous results
  ProfileSamplerChansRateScaleMix(num_input_chans, num_output_chans, sampler_type, source_rate,
                                  gain_type, false);

  // Accumulate with previous results
  ProfileSamplerChansRateScaleMix(num_input_chans, num_output_chans, sampler_type, source_rate,
                                  gain_type, true);
}

// Profile the samplers when mixing data types: uint8, int16, int24-in-32, float
void AudioPerformance::ProfileSamplerChansRateScaleMix(uint32_t num_input_chans,
                                                       uint32_t num_output_chans,
                                                       Resampler sampler_type, uint32_t source_rate,
                                                       GainType gain_type, bool accumulate) {
  ProfileMixer<uint8_t>(num_input_chans, num_output_chans, sampler_type, source_rate, gain_type,
                        accumulate);
  ProfileMixer<int16_t>(num_input_chans, num_output_chans, sampler_type, source_rate, gain_type,
                        accumulate);
  ProfileMixer<int32_t>(num_input_chans, num_output_chans, sampler_type, source_rate, gain_type,
                        accumulate);
  ProfileMixer<float>(num_input_chans, num_output_chans, sampler_type, source_rate, gain_type,
                      accumulate);
}

template <typename SampleType>
void AudioPerformance::ProfileMixer(uint32_t num_input_chans, uint32_t num_output_chans,
                                    Resampler sampler_type, uint32_t source_rate,
                                    GainType gain_type, bool accumulate) {
  fuchsia::media::AudioSampleFormat sample_format;
  double amplitude;
  std::string format;
  if (std::is_same_v<SampleType, uint8_t>) {
    sample_format = fuchsia::media::AudioSampleFormat::UNSIGNED_8;
    amplitude = std::numeric_limits<int8_t>::max();
    format = "Un8";
  } else if (std::is_same_v<SampleType, int16_t>) {
    sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
    amplitude = std::numeric_limits<int16_t>::max();
    format = "I16";
  } else if (std::is_same_v<SampleType, int32_t>) {
    sample_format = fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32;
    amplitude = std::numeric_limits<int32_t>::max() & ~0x0FF;
    format = "I24";
  } else if (std::is_same_v<SampleType, float>) {
    sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
    amplitude = 1.0;
    format = "F32";
  } else {
    ASSERT_TRUE(false) << "Unknown mix sample format for testing";
    return;
  }

  uint32_t dest_rate = 48000;
  auto mixer = SelectMixer(sample_format, num_input_chans, source_rate, num_output_chans, dest_rate,
                           sampler_type);
  if (mixer == nullptr) {
    return;
  }

  uint32_t source_buffer_size = kFreqTestBufSize * dest_rate / source_rate;
  uint32_t source_frames = source_buffer_size;

  auto source = std::make_unique<SampleType[]>(source_frames * num_input_chans);
  auto accum = std::make_unique<float[]>(kFreqTestBufSize * num_output_chans);
  uint32_t frac_src_frames = source_frames * Mixer::FRAC_ONE;
  int32_t frac_src_offset;
  uint32_t dest_offset, previous_dest_offset;

  OverwriteCosine(source.get(), source_buffer_size * num_input_chans,
                  FrequencySet::kReferenceFreqs[FrequencySet::kRefFreqIdx], amplitude);

  auto& info = mixer->bookkeeping();
  info.step_size = (source_rate * Mixer::FRAC_ONE) / dest_rate;
  info.denominator = dest_rate;
  info.rate_modulo = (source_rate * Mixer::FRAC_ONE) - (info.step_size * dest_rate);

  float gain_db;
  bool source_mute = false;

  char gain_char;
  switch (gain_type) {
    case GainType::Mute:
      // 0dB, Mute
      gain_db = Gain::kUnityGainDb;
      source_mute = true;
      gain_char = 'M';
      break;
    case GainType::Unity:
      // 0dB
      gain_db = Gain::kUnityGainDb;
      gain_char = 'U';
      break;
    case GainType::Scaled:
      // -42dB
      gain_db = -42.0f;
      gain_char = 'S';
      break;
    case GainType::Ramped:
      // -1dB => -159dB
      gain_db = Gain::kUnityGainDb - 1.0f;
      gain_char = 'R';
      break;
  }

  info.gain.SetDestGain(Gain::kUnityGainDb);
  auto width = mixer->pos_filter_width();

  zx::duration first, worst, best, total_elapsed{0};

  for (uint32_t i = 0; i < kNumMixerProfilerRuns; ++i) {
    info.gain.SetSourceGain(source_mute ? fuchsia::media::audio::MUTED_GAIN_DB : gain_db);

    if (gain_type == GainType::Ramped) {
      // Ramp within the "greater than Mute but less than Unity" range. Ramp duration assumes a mix
      // duration of less than two secs.
      info.gain.SetSourceGainWithRamp(Gain::kMinGainDb + 1.0f, zx::sec(2));
    }

    auto start_time = zx::clock::get_monotonic();

    dest_offset = 0;
    frac_src_offset = 0;
    info.src_pos_modulo = 0;

    while (dest_offset < kFreqTestBufSize) {
      previous_dest_offset = dest_offset;
      mixer->Mix(accum.get(), kFreqTestBufSize, &dest_offset, source.get(), frac_src_frames,
                 &frac_src_offset, accumulate);

      // Mix() might process less than all of accum, so Advance() after each.
      info.gain.Advance(dest_offset - previous_dest_offset, TimelineRate(source_rate, ZX_SEC(1)));
      if (frac_src_offset + width >= frac_src_frames) {
        frac_src_offset -= frac_src_frames;
      }
    }

    auto elapsed = zx::clock::get_monotonic() - start_time;

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

  char sampler_ch;
  switch (sampler_type) {
    case Resampler::SampleAndHold:
      sampler_ch = 'P';
      break;
    case Resampler::LinearInterpolation:
      sampler_ch = 'L';
      break;
    case Resampler::WindowedSinc:
      sampler_ch = 'W';
      break;
    case Resampler::Default:
      FX_LOGS(ERROR) << "Test should specify the Resampler exactly";
      return;
  }

  printf("%c-%s.%u%u%c%c%u:", sampler_ch, format.c_str(), num_input_chans, num_output_chans,
         gain_char, (accumulate ? '+' : '-'), source_rate);

  auto mean = total_elapsed / kNumMixerProfilerRuns;
  printf("\t%10.3lf\t%10.3lf\t%10.3lf\t%10.3lf\n", to_frac_usecs(mean), to_frac_usecs(first),
         to_frac_usecs(best), to_frac_usecs(worst));
}

void AudioPerformance::DisplayOutputColumnHeader() {
  printf("Config\t    Mean\t   First\t    Best\t   Worst\n");
}

void AudioPerformance::DisplayOutputConfigLegend() {
  printf("\n   Elapsed time in microsec to ProduceOutput() %u frames\n", kFreqTestBufSize);
  printf(
      "\n   For output configuration FFF-Rn, where:\n"
      "\t   FFF: Format of output data - Un8, I16, I24, F32,\n"
      "\t     R: Range of source data - [S]ilence, [O]ut-of-range, [N]ormal,\n"
      "\t     n: Number of output channels (one-digit number)\n\n");
}

void AudioPerformance::ProfileOutputProducers() {
  auto start_time = zx::clock::get_monotonic();

  DisplayOutputConfigLegend();
  DisplayOutputColumnHeader();

  ProfileOutputChans(1);
  ProfileOutputChans(2);
  ProfileOutputChans(4);
  ProfileOutputChans(8);

  DisplayOutputColumnHeader();
  DisplayOutputConfigLegend();

  printf("   Total time to profile OutputProducers: %lu ms\n   --------\n\n",
         (zx::clock::get_monotonic() - start_time).get() / ZX_MSEC(1));
}

void AudioPerformance::ProfileOutputChans(uint32_t num_chans) {
  ProfileOutputRange(num_chans, OutputDataRange::Silence);
  ProfileOutputRange(num_chans, OutputDataRange::OutOfRange);
  ProfileOutputRange(num_chans, OutputDataRange::Normal);
}

void AudioPerformance::ProfileOutputRange(uint32_t num_chans, OutputDataRange data_range) {
  ProfileOutputType<uint8_t>(num_chans, data_range);
  ProfileOutputType<int16_t>(num_chans, data_range);
  ProfileOutputType<int32_t>(num_chans, data_range);
  ProfileOutputType<float>(num_chans, data_range);
}

template <typename SampleType>
void AudioPerformance::ProfileOutputType(uint32_t num_chans, OutputDataRange data_range) {
  fuchsia::media::AudioSampleFormat sample_format;
  std::string format;
  char range;

  if (std::is_same_v<SampleType, uint8_t>) {
    sample_format = fuchsia::media::AudioSampleFormat::UNSIGNED_8;
    format = "Un8";
  } else if (std::is_same_v<SampleType, int16_t>) {
    sample_format = fuchsia::media::AudioSampleFormat::SIGNED_16;
    format = "I16";
  } else if (std::is_same_v<SampleType, int32_t>) {
    sample_format = fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32;
    format = "I24";
  } else if (std::is_same_v<SampleType, float>) {
    sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
    format = "F32";
  } else {
    ASSERT_TRUE(false) << "Unknown output sample format for testing";
    return;
  }

  auto output_producer = SelectOutputProducer(sample_format, num_chans);

  uint32_t num_samples = kFreqTestBufSize * num_chans;

  auto accum = std::make_unique<float[]>(num_samples);
  auto dest = std::make_unique<SampleType[]>(num_samples);

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

  zx::duration first, worst, best, total_elapsed{0};

  if (data_range == OutputDataRange::Silence) {
    for (uint32_t i = 0; i < kNumOutputProfilerRuns; ++i) {
      auto start_time = zx::clock::get_monotonic();

      output_producer->FillWithSilence(dest.get(), kFreqTestBufSize);
      auto elapsed = zx::clock::get_monotonic() - start_time;

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
      auto start_time = zx::clock::get_monotonic();

      output_producer->ProduceOutput(accum.get(), dest.get(), kFreqTestBufSize);
      auto elapsed = zx::clock::get_monotonic() - start_time;

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

  auto mean = total_elapsed / kNumOutputProfilerRuns;
  printf("%s-%c%u:\t%9.3lf\t%9.3lf\t%9.3lf\t%9.3lf\n", format.c_str(), range, num_chans,
         to_frac_usecs(mean), to_frac_usecs(first), to_frac_usecs(best), to_frac_usecs(worst));
}

}  // namespace media::audio::test

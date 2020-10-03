// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/test/audio_performance.h"

#include <lib/zx/clock.h>

#include <string>

#include "src/media/audio/audio_core/mixer/test/frequency_set.h"
#include "src/media/audio/audio_core/mixer/test/mixer_tests_shared.h"
#include "src/media/audio/lib/analysis/generators.h"
#include "src/media/audio/lib/format/audio_buffer.h"
#include "src/media/audio/lib/format/traits.h"

namespace media::audio::test {

float to_frac_usecs(zx::duration duration) {
  return static_cast<double>(duration.to_nsecs()) / 1000.0;
}

// Convenience abbreviation within this source file to shorten names
using Resampler = ::media::audio::Mixer::Resampler;
using ASF = fuchsia::media::AudioSampleFormat;

// For the given resampler, measure elapsed time over a number of mix jobs.
void AudioPerformance::Profile() {
  printf("\n\n Performance Profiling\n\n");

  AudioPerformance::ProfileMixerCreation();
  AudioPerformance::ProfileMixing();
  AudioPerformance::ProfileOutputProducers();
}

void AudioPerformance::ProfileMixerCreation() {
  auto start_time = zx::clock::get_monotonic();

  DisplayMixerCreationLegend();
  DisplayMixerCreationColumnHeader();

  ProfileMixerCreationType(Resampler::SampleAndHold);
  ProfileMixerCreationType(Resampler::LinearInterpolation);
  ProfileMixerCreationType(Resampler::WindowedSinc);

  DisplayMixerCreationColumnHeader();

  printf("   Total time to profile Mixer creation: %lu ms\n   --------\n\n",
         (zx::clock::get_monotonic() - start_time).get() / ZX_MSEC(1));
}

void AudioPerformance::DisplayMixerCreationLegend() {
  printf("\n   Elapsed time in microsec for a Mixer object to be created\n");
  printf(
      "\n   For mixer configuration R-fff.IO sssss:ddddd, where:\n"
      "\t     R: Resampler type - [P]oint, [L]inear, [W]indowed Sinc\n"
      "\t   fff: Format - un8, i16, i24, f32\n"
      "\t     I: Input channels (one-digit number)\n"
      "\t     O: Output channels (one-digit number)\n"
      "\t sssss: Source sample rate\n"
      "\t ddddd: Destination sample rate\n\n");
}

void AudioPerformance::DisplayMixerCreationColumnHeader() {
  printf("\nCreation config         \t     Mean\t    First\t     Best\t    Worst\tMean Cached\n");
}

void AudioPerformance::ProfileMixerCreationType(Resampler sampler_type) {
  ProfileMixerCreationTypeChan(sampler_type, 1, 1);
  ProfileMixerCreationTypeChan(sampler_type, 1, 4);

  ProfileMixerCreationTypeChan(sampler_type, 4, 1);
  ProfileMixerCreationTypeChan(sampler_type, 4, 4);
}

// skip some of the permutations, to optimize test running time
void AudioPerformance::ProfileMixerCreationTypeChan(Mixer::Resampler sampler_type,
                                                    uint32_t num_input_chans,
                                                    uint32_t num_output_chans) {
  ProfileMixerCreationTypeChanRate(sampler_type, num_input_chans, num_output_chans, 48000, 48000);
  if (num_input_chans == 4 && num_output_chans == 4) {
    ProfileMixerCreationTypeChanRate(sampler_type, num_input_chans, num_output_chans, 8000, 8000);

    ProfileMixerCreationTypeChanRate(sampler_type, num_input_chans, num_output_chans, 8000, 192000);
    ProfileMixerCreationTypeChanRate(sampler_type, num_input_chans, num_output_chans, 16000, 96000);
    ProfileMixerCreationTypeChanRate(sampler_type, num_input_chans, num_output_chans, 16000, 48000);

    ProfileMixerCreationTypeChanRate(sampler_type, num_input_chans, num_output_chans, 48000, 16000);
  }
  if (num_input_chans == 1 && num_output_chans == 1) {
    ProfileMixerCreationTypeChanRate(sampler_type, num_input_chans, num_output_chans, 192000,
                                     192000);

    ProfileMixerCreationTypeChanRate(sampler_type, num_input_chans, num_output_chans, 48000, 96000);

    ProfileMixerCreationTypeChanRate(sampler_type, num_input_chans, num_output_chans, 96000, 48000);
    ProfileMixerCreationTypeChanRate(sampler_type, num_input_chans, num_output_chans, 96000, 16000);
    ProfileMixerCreationTypeChanRate(sampler_type, num_input_chans, num_output_chans, 192000, 8000);
  }
}

// skip some of the permutations, to optimize test running time
void AudioPerformance::ProfileMixerCreationTypeChanRate(Mixer::Resampler sampler_type,
                                                        uint32_t num_input_chans,
                                                        uint32_t num_output_chans,
                                                        uint32_t source_rate, uint32_t dest_rate) {
  if (num_input_chans == 1 && num_output_chans == 1 && source_rate == 48000 && dest_rate == 48000) {
    ProfileMixerCreationTypeChanRateFormat(sampler_type, num_input_chans, num_output_chans,
                                           source_rate, dest_rate, ASF::UNSIGNED_8);
    ProfileMixerCreationTypeChanRateFormat(sampler_type, num_input_chans, num_output_chans,
                                           source_rate, dest_rate, ASF::SIGNED_16);
    ProfileMixerCreationTypeChanRateFormat(sampler_type, num_input_chans, num_output_chans,
                                           source_rate, dest_rate, ASF::SIGNED_24_IN_32);
  }
  ProfileMixerCreationTypeChanRateFormat(sampler_type, num_input_chans, num_output_chans,
                                         source_rate, dest_rate, ASF::FLOAT);
}

void AudioPerformance::ProfileMixerCreationTypeChanRateFormat(
    Mixer::Resampler sampler_type, uint32_t num_input_chans, uint32_t num_output_chans,
    uint32_t source_rate, uint32_t dest_rate, ASF sample_format) {
  zx::duration first, worst, best, total_elapsed{0}, total_elapsed_cached{0};

  for (uint32_t i = 0; i < kNumMixerCreationRuns; ++i) {
    auto t0 = zx::clock::get_monotonic();

    auto mixer1 = SelectMixer(sample_format, num_input_chans, source_rate, num_output_chans,
                              dest_rate, sampler_type);
    mixer1->EagerlyPrepare();
    auto t1 = zx::clock::get_monotonic();

    {
      auto elapsed = t1 - t0;
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

    auto mixer2 = SelectMixer(sample_format, num_input_chans, source_rate, num_output_chans,
                              dest_rate, sampler_type);
    mixer2->EagerlyPrepare();
    auto t2 = zx::clock::get_monotonic();
    total_elapsed_cached += t2 - t1;
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

  std::string format;
  if (sample_format == ASF::UNSIGNED_8) {
    format = "Un8";
  } else if (sample_format == ASF::SIGNED_16) {
    format = "I16";
  } else if (sample_format == ASF::SIGNED_24_IN_32) {
    format = "I24";
  } else if (sample_format == ASF::FLOAT) {
    format = "F32";
  } else {
    ASSERT_TRUE(false) << "Unknown mix sample format for testing";
    return;
  }

  printf("%c-%s.%u%u %6u:%6u: ", sampler_ch, format.c_str(), num_input_chans, num_output_chans,
         source_rate, dest_rate);

  auto mean = total_elapsed / kNumMixerCreationRuns;
  auto mean_cached = total_elapsed_cached / kNumMixerCreationRuns;
  printf("\t%10.3lf\t%10.3lf\t%10.3lf\t%10.3lf\t%10.3lf\n", to_frac_usecs(mean),
         to_frac_usecs(first), to_frac_usecs(best), to_frac_usecs(worst),
         to_frac_usecs(mean_cached));
}

void AudioPerformance::ProfileMixing() {
  auto start_time = zx::clock::get_monotonic();

  DisplayMixerConfigLegend();
  DisplayMixerColumnHeader();

  ProfileSampler(Resampler::SampleAndHold);
  ProfileSampler(Resampler::LinearInterpolation);
  ProfileSampler(Resampler::WindowedSinc);

  DisplayMixerColumnHeader();

  printf("   Total time to profile Mixing: %lu ms\n   --------\n\n",
         (zx::clock::get_monotonic() - start_time).get() / ZX_MSEC(1));
}

void AudioPerformance::DisplayMixerConfigLegend() {
  printf("\n   Elapsed time in microsec for Mix() to produce %u frames\n", kFreqTestBufSize);
  printf(
      "\n   For mixer configuration R-fff.IOGAnnnnn, where:\n"
      "\t     R: Resampler type - [P]oint, [L]inear, [W]indowed Sinc\n"
      "\t   fff: Format - un8, i16, i24, f32\n"
      "\t     I: Input channels (one-digit number)\n"
      "\t     O: Output channels (one-digit number)\n"
      "\t     G: Gain factor - [M]ute, [U]nity, [S]caled, [R]amping\n"
      "\t     A: Accumulate - [-] no or [+] yes\n"
      "\t nnnnn: Sample rate (five-digit number)\n\n");
}

void AudioPerformance::DisplayMixerColumnHeader() {
  printf("Configuration   \t     Mean\t    First\t     Best\t    Worst\n");
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
  ProfileMix<ASF::UNSIGNED_8>(num_input_chans, num_output_chans, sampler_type, source_rate,
                              gain_type, accumulate);
  ProfileMix<ASF::SIGNED_16>(num_input_chans, num_output_chans, sampler_type, source_rate,
                             gain_type, accumulate);
  ProfileMix<ASF::SIGNED_24_IN_32>(num_input_chans, num_output_chans, sampler_type, source_rate,
                                   gain_type, accumulate);
  ProfileMix<ASF::FLOAT>(num_input_chans, num_output_chans, sampler_type, source_rate, gain_type,
                         accumulate);
}

template <ASF SampleFormat>
void AudioPerformance::ProfileMix(uint32_t num_input_chans, uint32_t num_output_chans,
                                  Resampler sampler_type, uint32_t source_rate, GainType gain_type,
                                  bool accumulate) {
  double amplitude;
  std::string format;
  if constexpr (SampleFormat == ASF::UNSIGNED_8) {
    amplitude = std::numeric_limits<int8_t>::max();
    format = "Un8";
  } else if constexpr (SampleFormat == ASF::SIGNED_16) {
    amplitude = std::numeric_limits<int16_t>::max();
    format = "I16";
  } else if constexpr (SampleFormat == ASF::SIGNED_24_IN_32) {
    amplitude = std::numeric_limits<int32_t>::max() & ~0x0FF;
    format = "I24";
  } else if constexpr (SampleFormat == ASF::FLOAT) {
    amplitude = 1.0;
    format = "F32";
  } else {
    ASSERT_TRUE(false) << "Unknown mix sample format for testing";
    return;
  }

  uint32_t dest_rate = 48000;
  auto mixer = SelectMixer(SampleFormat, num_input_chans, source_rate, num_output_chans, dest_rate,
                           sampler_type);
  if (mixer == nullptr) {
    return;
  }

  auto source_format = Format::Create<SampleFormat>(num_input_chans, source_rate).take_value();
  uint32_t source_buffer_size = kFreqTestBufSize * dest_rate / source_rate;
  uint32_t source_frames = source_buffer_size;

  auto source =
      GenerateCosineAudio(source_format, source_frames,
                          FrequencySet::kReferenceFreqs[FrequencySet::kRefFreqIdx], amplitude);

  auto accum = std::make_unique<float[]>(kFreqTestBufSize * num_output_chans);
  uint32_t frac_src_frames = source_frames * Mixer::FRAC_ONE;
  int32_t frac_src_offset;
  uint32_t dest_offset, previous_dest_offset;

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
  auto width = mixer->pos_filter_width().raw_value();

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
      mixer->Mix(accum.get(), kFreqTestBufSize, &dest_offset, &source.samples()[0], frac_src_frames,
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

void AudioPerformance::ProfileOutputProducers() {
  auto start_time = zx::clock::get_monotonic();

  DisplayOutputConfigLegend();
  DisplayOutputColumnHeader();

  ProfileOutputChans(1);
  ProfileOutputChans(2);
  ProfileOutputChans(4);
  ProfileOutputChans(8);

  DisplayOutputColumnHeader();

  printf("   Total time to profile OutputProducers: %lu ms\n   --------\n\n",
         (zx::clock::get_monotonic() - start_time).get() / ZX_MSEC(1));
}

void AudioPerformance::DisplayOutputConfigLegend() {
  printf("\n   Elapsed time in microsec to ProduceOutput() %u frames\n", kFreqTestBufSize);
  printf(
      "\n   For output configuration FFF-Rn, where:\n"
      "\t   FFF: Format of output data - Un8, I16, I24, F32\n"
      "\t     R: Range of source data - [S]ilence, [O]ut-of-range, [N]ormal\n"
      "\t     n: Number of output channels (one-digit number)\n\n");
}

void AudioPerformance::DisplayOutputColumnHeader() {
  printf("Config\t    Mean\t   First\t    Best\t   Worst\n");
}

void AudioPerformance::ProfileOutputChans(uint32_t num_chans) {
  ProfileOutputRange(num_chans, OutputDataRange::Silence);
  ProfileOutputRange(num_chans, OutputDataRange::OutOfRange);
  ProfileOutputRange(num_chans, OutputDataRange::Normal);
}

void AudioPerformance::ProfileOutputRange(uint32_t num_chans, OutputDataRange data_range) {
  ProfileOutputType<ASF::UNSIGNED_8>(num_chans, data_range);
  ProfileOutputType<ASF::SIGNED_16>(num_chans, data_range);
  ProfileOutputType<ASF::SIGNED_24_IN_32>(num_chans, data_range);
  ProfileOutputType<ASF::FLOAT>(num_chans, data_range);
}

template <ASF SampleFormat>
void AudioPerformance::ProfileOutputType(uint32_t num_chans, OutputDataRange data_range) {
  std::string format;
  char range;

  if constexpr (SampleFormat == ASF::UNSIGNED_8) {
    format = "Un8";
  } else if constexpr (SampleFormat == ASF::SIGNED_16) {
    format = "I16";
  } else if constexpr (SampleFormat == ASF::SIGNED_24_IN_32) {
    format = "I24";
  } else if constexpr (SampleFormat == ASF::FLOAT) {
    format = "F32";
  } else {
    ASSERT_TRUE(false) << "Unknown output sample format for testing";
    return;
  }

  auto output_producer = SelectOutputProducer(SampleFormat, num_chans);

  using SampleT = typename SampleFormatTraits<SampleFormat>::SampleT;
  uint32_t num_samples = kFreqTestBufSize * num_chans;
  auto dest = std::make_unique<SampleT[]>(num_samples);

  auto accum_format = Format::Create<ASF::FLOAT>(num_chans, 48000 /* unused */).take_value();
  AudioBuffer accum(accum_format, 0);

  switch (data_range) {
    case OutputDataRange::Silence:
      range = 'S';
      accum = GenerateSilentAudio(accum_format, kFreqTestBufSize);
      break;
    case OutputDataRange::OutOfRange:
      range = 'O';
      accum = AudioBuffer(accum_format, kFreqTestBufSize);
      for (size_t idx = 0; idx < num_samples; ++idx) {
        accum.samples()[idx] = (idx % 2 ? -1.5f : 1.5f);
      }
      break;
    case OutputDataRange::Normal:
      range = 'N';
      accum = GenerateCosineAudio(accum_format, kFreqTestBufSize,
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

      output_producer->ProduceOutput(&accum.samples()[0], dest.get(), kFreqTestBufSize);
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

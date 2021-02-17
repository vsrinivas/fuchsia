// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/tools/audio_performance.h"

#include <lib/zx/clock.h>

#include <string>

#include "src/media/audio/audio_core/mixer/test/frequency_set.h"
#include "src/media/audio/lib/analysis/generators.h"
#include "src/media/audio/lib/format/audio_buffer.h"
#include "src/media/audio/lib/format/traits.h"

namespace media::audio::tools {

float to_frac_usecs(zx::duration duration) {
  return static_cast<double>(duration.to_nsecs()) / 1000.0;
}

// Convenience abbreviation within this source file to shorten names
using Resampler = ::media::audio::Mixer::Resampler;
using ASF = fuchsia::media::AudioSampleFormat;

const uint32_t AudioPerformance::kProfilerBufferSize = ::media::audio::test::kFreqTestBufSize;
const uint32_t AudioPerformance::kProfilerFrequency = ::media::audio::test::FrequencySet::
    kReferenceFreqs[::media::audio::test::FrequencySet::kRefFreqIdx];

BenchmarkType AudioPerformance::benchmark_type_ = BenchmarkType::All;

//
// Subtest utility functions -- used by test functions; can ASSERT on their own.
//
// Find a suitable mixer for the provided format, channels and frame rates.
// In testing, we choose ratio-of-frame-rates and source_channels carefully, to
// trigger the selection of a specific mixer. Note: Mixers convert audio into
// our accumulation format (not the destination format), so we need not specify
// a dest_format. Actual frame rate values are unimportant, but inter-rate RATIO
// is VERY important: required SRC is the primary factor in Mix selection.
std::unique_ptr<Mixer> SelectMixer(fuchsia::media::AudioSampleFormat source_format,
                                   uint32_t source_channels, uint32_t source_frame_rate,
                                   uint32_t dest_channels, uint32_t dest_frame_rate,
                                   Resampler resampler) {
  if (resampler == Resampler::Default) {
    FX_LOGS(FATAL) << "Profiler should specify the Resampler exactly";
    return nullptr;
  }

  fuchsia::media::AudioStreamType source_details;
  source_details.sample_format = source_format;
  source_details.channels = source_channels;
  source_details.frames_per_second = source_frame_rate;

  fuchsia::media::AudioStreamType dest_details;
  dest_details.sample_format = fuchsia::media::AudioSampleFormat::FLOAT;
  dest_details.channels = dest_channels;
  dest_details.frames_per_second = dest_frame_rate;

  return Mixer::Select(source_details, dest_details, resampler);
}

// Just as Mixers convert audio into our accumulation format, OutputProducer objects exist to
// format-convert audio frames during the copy from accumulator to destination. They perform no
// rate-conversion, gain scaling or rechannelization, so frames_per_second is unreferenced.
// Num_channels and sample_format are used, to calculate the size of a (multi-channel) audio frame.
std::unique_ptr<OutputProducer> SelectOutputProducer(fuchsia::media::AudioSampleFormat dest_format,
                                                     uint32_t num_channels) {
  fuchsia::media::AudioStreamType dest_details;
  dest_details.sample_format = dest_format;
  dest_details.channels = num_channels;
  dest_details.frames_per_second = 48000;  // unreferenced - see comment above.

  return OutputProducer::Select(dest_details);
}

// Measure elapsed time over certain mixer-related operations.
int AudioPerformance::Profile() {
  printf("\n\n Performance Profiling\n\n");

  switch (benchmark_type_) {
    case BenchmarkType::CreationOnly:
      AudioPerformance::ProfileMixerCreation();
      break;

    case BenchmarkType::MixingOnly:
      AudioPerformance::ProfileMixing();
      break;

    case BenchmarkType::OutputOnly:
      AudioPerformance::ProfileOutputProducers();
      break;

    case BenchmarkType::All:
      AudioPerformance::ProfileMixerCreation();
      AudioPerformance::ProfileMixing();
      AudioPerformance::ProfileOutputProducers();
      break;
  }

  return 0;
}

void AudioPerformance::ProfileMixerCreation() {
  auto start_time = zx::clock::get_monotonic();

  DisplayMixerCreationLegend();
  DisplayMixerCreationColumnHeader();

  ProfileMixerCreationType(Resampler::SampleAndHold);
  ProfileMixerCreationType(Resampler::WindowedSinc);

  DisplayMixerCreationColumnHeader();

  printf("   Total time to profile Mixer creation (%u iterations): %lu ms\n   --------\n\n",
         kNumMixerCreationRuns, (zx::clock::get_monotonic() - start_time).get() / ZX_MSEC(1));
}

void AudioPerformance::DisplayMixerCreationLegend() {
  printf("\n   Elapsed time in microsec for a Mixer object to be created\n");
  printf(
      "\n   For mixer configuration R-fff.IO sssss:ddddd, where:\n"
      "\t     R: Resampler type - [P]oint, [W]indowed Sinc\n"
      "\t   fff: Format - un8, i16, i24, f32\n"
      "\t     I: Input channels (one-digit number)\n"
      "\t     O: Output channels (one-digit number)\n"
      "\t sssss: Source sample rate\n"
      "\t ddddd: Destination sample rate\n");
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
    if (!mixer1) {
      return;
    }

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
    case Resampler::WindowedSinc:
      sampler_ch = 'W';
      break;
    case Resampler::Default:
      FX_LOGS(FATAL) << "Profiler should specify the Resampler exactly";
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
    FX_LOGS(FATAL) << "Unknown sample format for creation profiling";
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
  ProfileSampler(Resampler::WindowedSinc);

  DisplayMixerColumnHeader();

  printf("   Total time to profile Mixing (%u iterations): %lu ms\n   --------\n\n",
         kNumMixerProfilerRuns, (zx::clock::get_monotonic() - start_time).get() / ZX_MSEC(1));
}

void AudioPerformance::DisplayMixerConfigLegend() {
  printf("\n   Elapsed time in microsec for Mix() to produce %u frames\n", kProfilerBufferSize);
  printf(
      "\n   For mixer configuration R-fff.IOGAnnnnn, where:\n"
      "\t     R: Resampler type - [P]oint, [W]indowed Sinc\n"
      "\t   fff: Format - un8, i16, i24, f32\n"
      "\t     I: Input channels (one-digit number)\n"
      "\t     O: Output channels (one-digit number)\n"
      "\t     G: Gain factor - [M]ute, [U]nity, [S]caled, [R]amping\n"
      "\t     A: Accumulate - [-] no or [+] yes\n"
      "\t nnnnn: Sample rate (five-digit number)\n");
}

void AudioPerformance::DisplayMixerColumnHeader() {
  printf("\nConfiguration   \t     Mean\t    First\t     Best\t    Worst\n");
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

  if (sampler_type != Resampler::SampleAndHold) {
    ProfileSamplerChansRate(num_input_chans, num_output_chans, sampler_type, 44100);
    ProfileSamplerChansRate(num_input_chans, num_output_chans, sampler_type, 192000);
  }
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
    FX_LOGS(FATAL) << "Unknown sample format for mix profiling";
    return;
  }

  uint32_t dest_rate = 48000;
  auto mixer = SelectMixer(SampleFormat, num_input_chans, source_rate, num_output_chans, dest_rate,
                           sampler_type);
  if (mixer == nullptr) {
    return;
  }

  auto source_format = Format::Create<SampleFormat>(num_input_chans, source_rate).take_value();
  uint32_t source_frame_count = kProfilerBufferSize * source_rate / dest_rate;

  auto source =
      GenerateCosineAudio(source_format, source_frame_count, kProfilerFrequency, amplitude);

  auto accum = std::make_unique<float[]>(kProfilerBufferSize * num_output_chans);
  uint32_t frac_source_frames = source_frame_count * Mixer::FRAC_ONE;
  uint32_t dest_offset, previous_dest_offset;

  auto& info = mixer->bookkeeping();
  info.step_size = (source_rate * Mixer::FRAC_ONE) / dest_rate;
  info.SetRateModuloAndDenominator((source_rate * Mixer::FRAC_ONE) - (info.step_size * dest_rate),
                                   dest_rate);

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
    int32_t frac_source_offset = 0;
    info.source_pos_modulo = 0;

    while (dest_offset < kProfilerBufferSize) {
      previous_dest_offset = dest_offset;
      bool buffer_done =
          mixer->Mix(accum.get(), kProfilerBufferSize, &dest_offset, &source.samples()[0],
                     frac_source_frames, &frac_source_offset, accumulate);

      // Mix() might process less than all of accum, so Advance() after each.
      info.gain.Advance(dest_offset - previous_dest_offset, TimelineRate(source_rate, ZX_SEC(1)));
      if (buffer_done) {
        frac_source_offset -= frac_source_frames;
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
    case Resampler::WindowedSinc:
      sampler_ch = 'W';
      break;
    case Resampler::Default:
      FX_LOGS(FATAL) << "Profiler should specify the Resampler exactly";
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

  printf("   Total time to profile OutputProducers (%u iterations): %lu ms\n   --------\n\n",
         kNumOutputProfilerRuns, (zx::clock::get_monotonic() - start_time).get() / ZX_MSEC(1));
}

void AudioPerformance::DisplayOutputConfigLegend() {
  printf("\n   Elapsed time in microsec to ProduceOutput() %u frames\n", kProfilerBufferSize);
  printf(
      "\n   For output configuration FFF-Rn, where:\n"
      "\t   FFF: Format of output data - Un8, I16, I24, F32\n"
      "\t     R: Range of source data - [S]ilence, [O]ut-of-range, [N]ormal\n"
      "\t     n: Number of output channels (one-digit number)\n");
}

void AudioPerformance::DisplayOutputColumnHeader() {
  printf("\nConfig\t    Mean\t   First\t    Best\t   Worst\n");
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
    FX_LOGS(FATAL) << "Unknown output sample format for profiling";
    return;
  }

  auto output_producer = SelectOutputProducer(SampleFormat, num_chans);
  if (!output_producer) {
    return;
  }

  using SampleT = typename SampleFormatTraits<SampleFormat>::SampleT;
  uint32_t num_samples = kProfilerBufferSize * num_chans;
  auto dest = std::make_unique<SampleT[]>(num_samples);

  auto accum_format = Format::Create<ASF::FLOAT>(num_chans, 48000 /* unused */).take_value();
  AudioBuffer accum(accum_format, 0);

  switch (data_range) {
    case OutputDataRange::Silence:
      range = 'S';
      accum = GenerateSilentAudio(accum_format, kProfilerBufferSize);
      break;
    case OutputDataRange::OutOfRange:
      range = 'O';
      accum = AudioBuffer(accum_format, kProfilerBufferSize);
      for (size_t idx = 0; idx < num_samples; ++idx) {
        accum.samples()[idx] = (idx % 2 ? -1.5f : 1.5f);
      }
      break;
    case OutputDataRange::Normal:
      range = 'N';
      accum = GenerateCosineAudio(accum_format, kProfilerBufferSize, kProfilerFrequency);
      break;
    default:
      FX_LOGS(FATAL) << "Unknown output data range for profiling";
      return;
  }

  zx::duration first, worst, best, total_elapsed{0};

  if (data_range == OutputDataRange::Silence) {
    for (uint32_t i = 0; i < kNumOutputProfilerRuns; ++i) {
      auto start_time = zx::clock::get_monotonic();

      output_producer->FillWithSilence(dest.get(), kProfilerBufferSize);
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

      output_producer->ProduceOutput(&accum.samples()[0], dest.get(), kProfilerBufferSize);
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

}  // namespace media::audio::tools

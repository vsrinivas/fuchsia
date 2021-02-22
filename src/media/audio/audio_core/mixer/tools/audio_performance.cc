// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/tools/audio_performance.h"

#include <lib/zx/clock.h>

#include <string>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/media/audio/audio_core/mixer/test/frequency_set.h"
#include "src/media/audio/lib/analysis/generators.h"
#include "src/media/audio/lib/format/audio_buffer.h"
#include "src/media/audio/lib/format/traits.h"

// Convenience abbreviation within this source file to shorten names
using Resampler = ::media::audio::Mixer::Resampler;
using ASF = fuchsia::media::AudioSampleFormat;

namespace media::audio::tools {
namespace {

float to_usecs(zx::duration duration) { return static_cast<double>(duration.to_nsecs()) / 1000.0; }

const zx::duration kMixLength = zx::msec(10);

// Records the performance of multiple runs and produces statistics.
struct Stats {
  int64_t runs = 0;
  zx::duration first;
  zx::duration worst;
  zx::duration best;
  zx::duration total;

  zx::duration mean() { return total / runs; }

  void Add(zx::duration elapsed) {
    if (runs > 0) {
      worst = std::max(worst, elapsed);
      best = std::min(best, elapsed);
    } else {
      first = elapsed;
      worst = elapsed;
      best = elapsed;
    }
    total += elapsed;
    runs++;
  }

  std::string Summary() {
    return fxl::StringPrintf("%10.3lf\t%10.3lf\t%10.3lf\t%10.3lf", to_usecs(mean()),
                             to_usecs(first), to_usecs(best), to_usecs(worst));
  }
};

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
  dest_details.frames_per_second = 48000;

  return OutputProducer::Select(dest_details);
}

}  // namespace

std::string AudioPerformance::MixerConfig::ToStringForCreate() const {
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
      return "";
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
    return "";
  }

  return fxl::StringPrintf("%c-%s.%u%u %6u:%6u", sampler_ch, format.c_str(), num_input_chans,
                           num_output_chans, source_rate, dest_rate);
}

std::string AudioPerformance::MixerConfig::ToStringForMixer() const {
  char gain_char;
  switch (gain_type) {
    case GainType::Mute:
      gain_char = 'M';
      break;
    case GainType::Unity:
      gain_char = 'U';
      break;
    case GainType::Scaled:
      gain_char = 'S';
      break;
    case GainType::Ramped:
      gain_char = 'R';
      break;
  }

  return fxl::StringPrintf("%s %c%c", ToStringForCreate().c_str(), gain_char,
                           (accumulate ? '+' : '-'));
}

std::string AudioPerformance::OutputProducerConfig::ToString() const {
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
    return "";
  }

  char range;
  switch (input_range) {
    case InputRange::Silence:
      range = 'S';
      break;
    case InputRange::OutOfRange:
      range = 'O';
      break;
    case InputRange::Normal:
      range = 'N';
      break;
  }

  return fxl::StringPrintf("%s-%c%u", format.c_str(), range, num_chans);
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
      "\t ddddd: Destination sample rate\n\n");
}

void AudioPerformance::DisplayMixerCreationColumnHeader() {
  printf(
      "\nCreation config        \t      Mean\t     First\t      Best\t     Worst\tMean Cached\n");
}

void AudioPerformance::ProfileMixerCreation(const std::vector<MixerConfig>& configs,
                                            zx::duration duration_per_config) {
  auto start_time = zx::clock::get_monotonic();

  DisplayMixerCreationLegend();
  DisplayMixerCreationColumnHeader();

  for (auto& cfg : configs) {
    ProfileMixerCreation(cfg, duration_per_config);
  }

  DisplayMixerCreationColumnHeader();
  printf("   Total time to profile Mixer creation: %lu ms\n   --------\n\n",
         (zx::clock::get_monotonic() - start_time).get() / ZX_MSEC(1));
}

void AudioPerformance::ProfileMixerCreation(const MixerConfig& cfg,
                                            const zx::duration total_duration) {
  Stats cold_cache;
  Stats warm_cache;

  while (cold_cache.total < total_duration) {
    auto t0 = zx::clock::get_monotonic();

    auto mixer1 = SelectMixer(cfg.sample_format, cfg.num_input_chans, cfg.source_rate,
                              cfg.num_output_chans, cfg.dest_rate, cfg.sampler_type);
    if (!mixer1) {
      return;
    }

    mixer1->EagerlyPrepare();
    auto t1 = zx::clock::get_monotonic();
    cold_cache.Add(t1 - t0);

    auto mixer2 = SelectMixer(cfg.sample_format, cfg.num_input_chans, cfg.source_rate,
                              cfg.num_output_chans, cfg.dest_rate, cfg.sampler_type);
    mixer2->EagerlyPrepare();
    auto t2 = zx::clock::get_monotonic();
    warm_cache.Add(t2 - t1);
  }

  printf("%s:\t%s\t%10.3lf\n", cfg.ToStringForCreate().c_str(), cold_cache.Summary().c_str(),
         to_usecs(warm_cache.mean()));
}

void AudioPerformance::DisplayMixerLegend() {
  printf("\n   Elapsed time in microsec for Mix() to produce %ldms of frames\n",
         kMixLength.to_msecs());
  printf(
      "\n   For mixer configuration R-fff.IO sssss:ddddd GA, where:\n"
      "\t     R: Resampler type - [P]oint, [W]indowed Sinc\n"
      "\t   fff: Format - un8, i16, i24, f32\n"
      "\t     I: Input channels (one-digit number)\n"
      "\t     O: Output channels (one-digit number)\n"
      "\t sssss: Source sample rate\n"
      "\t ddddd: Destination sample rate\n\n"
      "\t     G: Gain factor - [M]ute, [U]nity, [S]caled, [R]amping\n"
      "\t     A: Accumulate - [-] no or [+] yes\n\n");
}

void AudioPerformance::DisplayMixerColumnHeader() {
  printf("Configuration             \t     Mean\t    First\t     Best\t    Worst\n");
}

void AudioPerformance::ProfileMixer(const std::vector<MixerConfig>& configs,
                                    zx::duration duration_per_config) {
  auto start_time = zx::clock::get_monotonic();

  DisplayMixerLegend();
  DisplayMixerColumnHeader();

  for (auto& cfg : configs) {
    switch (cfg.sample_format) {
      case ASF::UNSIGNED_8:
        ProfileMixer<ASF::UNSIGNED_8>(cfg, duration_per_config);
        break;
      case ASF::SIGNED_16:
        ProfileMixer<ASF::SIGNED_16>(cfg, duration_per_config);
        break;
      case ASF::SIGNED_24_IN_32:
        ProfileMixer<ASF::SIGNED_24_IN_32>(cfg, duration_per_config);
        break;
      case ASF::FLOAT:
        ProfileMixer<ASF::FLOAT>(cfg, duration_per_config);
        break;
    }
  }

  DisplayMixerColumnHeader();
  printf("   Total time to profile Mixer: %lu ms\n   --------\n\n",
         (zx::clock::get_monotonic() - start_time).get() / ZX_MSEC(1));
}

template <ASF SampleFormat>
void AudioPerformance::ProfileMixer(const MixerConfig& cfg, const zx::duration total_duration) {
  FX_CHECK(SampleFormat == cfg.sample_format);

  double amplitude;
  if constexpr (SampleFormat == ASF::UNSIGNED_8) {
    amplitude = std::numeric_limits<int8_t>::max();
  } else if constexpr (SampleFormat == ASF::SIGNED_16) {
    amplitude = std::numeric_limits<int16_t>::max();
  } else if constexpr (SampleFormat == ASF::SIGNED_24_IN_32) {
    amplitude = std::numeric_limits<int32_t>::max() & ~0x0FF;
  } else if constexpr (SampleFormat == ASF::FLOAT) {
    amplitude = 1.0;
  } else {
    FX_LOGS(FATAL) << "Unknown sample format for mix profiling";
    return;
  }

  auto mixer = SelectMixer(SampleFormat, cfg.num_input_chans, cfg.source_rate, cfg.num_output_chans,
                           cfg.dest_rate, cfg.sampler_type);
  if (mixer == nullptr) {
    return;
  }

  // Allocate enough source and destination frames for kMixLength.
  // When allocating source frames, we round up to ensure we have enough source frames.
  const uint32_t dest_frame_count =
      TimelineRate(cfg.dest_rate, 1'000'000'000)
          .Scale(kMixLength.to_nsecs(), TimelineRate::RoundingMode::Floor);
  const uint32_t source_frame_count =
      TimelineRate(cfg.source_rate, 1'000'000'000)
          .Scale(kMixLength.to_nsecs(), TimelineRate::RoundingMode::Ceiling);

  auto source_format =
      Format::Create<SampleFormat>(cfg.num_input_chans, cfg.source_rate).take_value();

  // This is a 1kHz sine wave, but the actual data doesn't matter.
  const auto periods = TimelineRate(1000, 1'000'000'000).Scale(kMixLength.to_nsecs());
  auto source = GenerateCosineAudio(source_format, source_frame_count, periods, amplitude);

  auto accum = std::make_unique<float[]>(dest_frame_count * cfg.num_output_chans);
  uint32_t frac_source_frames = source_frame_count * Mixer::FRAC_ONE;
  uint32_t dest_offset, previous_dest_offset;

  auto& info = mixer->bookkeeping();
  info.step_size = (cfg.source_rate * Mixer::FRAC_ONE) / cfg.dest_rate;
  info.SetRateModuloAndDenominator(
      (cfg.source_rate * Mixer::FRAC_ONE) - (info.step_size * cfg.dest_rate), cfg.dest_rate);

  float gain_db;
  bool source_mute = false;
  switch (cfg.gain_type) {
    case GainType::Mute:
      // 0dB, Mute
      gain_db = Gain::kUnityGainDb;
      source_mute = true;
      break;
    case GainType::Unity:
      // 0dB
      gain_db = Gain::kUnityGainDb;
      break;
    case GainType::Scaled:
      // -42dB
      gain_db = -42.0f;
      break;
    case GainType::Ramped:
      // -1dB => -159dB
      gain_db = Gain::kUnityGainDb - 1.0f;
      break;
  }

  info.gain.SetDestGain(Gain::kUnityGainDb);

  Stats stats;
  while (stats.total < total_duration) {
    info.gain.SetSourceGain(source_mute ? fuchsia::media::audio::MUTED_GAIN_DB : gain_db);

    if (cfg.gain_type == GainType::Ramped) {
      // Ramp within the "greater than Mute but less than Unity" range. Ramp duration assumes a mix
      // duration of less than two secs.
      info.gain.SetSourceGainWithRamp(Gain::kMinGainDb + 1.0f, zx::sec(2));
    }

    auto t0 = zx::clock::get_monotonic();

    dest_offset = 0;
    int32_t frac_source_offset = 0;
    info.source_pos_modulo = 0;

    while (dest_offset < dest_frame_count) {
      previous_dest_offset = dest_offset;
      bool buffer_done =
          mixer->Mix(accum.get(), dest_frame_count, &dest_offset, &source.samples()[0],
                     frac_source_frames, &frac_source_offset, cfg.accumulate);

      // Mix() might process less than all of accum, so Advance() after each.
      info.gain.Advance(dest_offset - previous_dest_offset,
                        TimelineRate(cfg.source_rate, ZX_SEC(1)));

      if (buffer_done) {
        frac_source_offset -= frac_source_frames;
      }
    }

    auto t1 = zx::clock::get_monotonic();
    stats.Add(t1 - t0);
  }

  printf("%s:\t%s\n", cfg.ToStringForMixer().c_str(), stats.Summary().c_str());
}

void AudioPerformance::DisplayOutputConfigLegend() {
  printf("\n   Elapsed time in microsec to ProduceOutput() %ldms of frames\n",
         kMixLength.to_msecs());
  printf(
      "\n   For output configuration FFF-Rn, where:\n"
      "\t   FFF: Format of output data - Un8, I16, I24, F32\n"
      "\t     R: Range of source data - [S]ilence, [O]ut-of-range, [N]ormal\n"
      "\t     n: Number of output channels (one-digit number)\n\n");
}

void AudioPerformance::DisplayOutputColumnHeader() {
  printf("Config\t     Mean\t    First\t     Best\t    Worst\n");
}

void AudioPerformance::ProfileOutputProducer(const std::vector<OutputProducerConfig>& configs,
                                             zx::duration duration_per_config) {
  auto start_time = zx::clock::get_monotonic();

  DisplayOutputConfigLegend();
  DisplayOutputColumnHeader();

  for (auto& cfg : configs) {
    switch (cfg.sample_format) {
      case ASF::UNSIGNED_8:
        ProfileOutputProducer<ASF::UNSIGNED_8>(cfg, duration_per_config);
        break;
      case ASF::SIGNED_16:
        ProfileOutputProducer<ASF::SIGNED_16>(cfg, duration_per_config);
        break;
      case ASF::SIGNED_24_IN_32:
        ProfileOutputProducer<ASF::SIGNED_24_IN_32>(cfg, duration_per_config);
        break;
      case ASF::FLOAT:
        ProfileOutputProducer<ASF::FLOAT>(cfg, duration_per_config);
        break;
    }
  }

  DisplayOutputColumnHeader();
  printf("   Total time to profile OutputProducer: %lu ms\n   --------\n\n",
         (zx::clock::get_monotonic() - start_time).get() / ZX_MSEC(1));
}

template <ASF SampleFormat>
void AudioPerformance::ProfileOutputProducer(const OutputProducerConfig& cfg,
                                             const zx::duration total_duration) {
  FX_CHECK(SampleFormat == cfg.sample_format);

  auto output_producer = SelectOutputProducer(SampleFormat, cfg.num_chans);
  if (!output_producer) {
    return;
  }

  // Produce 10ms worth of output at 48kHz.
  using SampleT = typename SampleFormatTraits<SampleFormat>::SampleT;
  uint32_t frame_count = TimelineRate(48000, 1'000'000'000).Scale(kMixLength.to_nsecs());
  uint32_t num_samples = frame_count * cfg.num_chans;
  auto dest = std::make_unique<SampleT[]>(num_samples);

  Stats stats;

  if (cfg.input_range == InputRange::Silence) {
    while (stats.total < total_duration) {
      auto t0 = zx::clock::get_monotonic();
      output_producer->FillWithSilence(dest.get(), frame_count);
      auto t1 = zx::clock::get_monotonic();
      stats.Add(t1 - t0);
    }
  } else {
    auto accum_format = Format::Create<ASF::FLOAT>(cfg.num_chans, 48000 /* unused */).take_value();
    AudioBuffer accum(accum_format, 0);

    switch (cfg.input_range) {
      case InputRange::OutOfRange:
        accum = AudioBuffer(accum_format, frame_count);
        for (uint32_t idx = 0; idx < num_samples; ++idx) {
          accum.samples()[idx] = (idx % 2 ? -1.5f : 1.5f);
        }
        break;
      case InputRange::Normal: {
        // This is a 1kHz sine wave, but the actual shape doesn't matter.
        // We use an amplitude < 1.0 to avoid code that clamps +1.0 values on integer outputs.
        const auto periods = TimelineRate(1000, 1'000'000'000).Scale(kMixLength.to_nsecs());
        accum = GenerateCosineAudio(accum_format, frame_count, periods, 0.9);
        break;
      }
      default:
        FX_LOGS(FATAL) << "Unknown output type: " << static_cast<int>(cfg.input_range);
        return;
    }

    while (stats.total < total_duration) {
      auto t0 = zx::clock::get_monotonic();
      output_producer->ProduceOutput(&accum.samples()[0], dest.get(), frame_count);
      auto t1 = zx::clock::get_monotonic();
      stats.Add(t1 - t0);
    }
  }

  printf("%s:\t%s\n", cfg.ToString().c_str(), stats.Summary().c_str());
}

}  // namespace media::audio::tools

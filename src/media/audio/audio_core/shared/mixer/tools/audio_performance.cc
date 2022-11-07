// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/shared/mixer/tools/audio_performance.h"

#include <lib/zx/clock.h>

#include <string>

#include "src/lib/fxl/strings/string_printf.h"
#include "src/media/audio/audio_core/shared/mixer/test/frequency_set.h"
#include "src/media/audio/lib/analysis/generators.h"
#include "src/media/audio/lib/format/audio_buffer.h"
#include "src/media/audio/lib/format/traits.h"
#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/lib/timeline/timeline_rate.h"

// Convenience abbreviation within this source file to shorten names
using Resampler = ::media::audio::Mixer::Resampler;
using ASF = fuchsia::media::AudioSampleFormat;

namespace media::audio::tools {
namespace {

float to_usecs(zx::duration duration) { return static_cast<float>(duration.to_nsecs()) / 1000.0f; }

std::string AsfToString(const ASF& sample_format, bool abbreviate) {
  if (sample_format == ASF::UNSIGNED_8) {
    return abbreviate ? "un8" : "Unsigned_8";
  } else if (sample_format == ASF::SIGNED_16) {
    return abbreviate ? "i16" : "Signed_16";
  } else if (sample_format == ASF::SIGNED_24_IN_32) {
    return abbreviate ? "i24" : "Signed_24_In_32";
  } else if (sample_format == ASF::FLOAT) {
    return abbreviate ? "f32" : "Float";
  } else {
    FX_LOGS(FATAL) << "Unknown sample format for creation profiling: "
                   << static_cast<int64_t>(sample_format);
    return "";
  }
}

const zx::duration kMixLength = zx::msec(10);

// Records the performance of multiple runs and produces statistics.
struct Stats {
  explicit Stats(perftest::TestCaseResults* result = nullptr) : perftest_result(result) {}

  int64_t runs = 0;
  zx::duration first;
  zx::duration worst;
  zx::duration best;
  zx::duration total;
  perftest::TestCaseResults* perftest_result;

  zx::duration mean() { return total / runs; }

  void Add(zx::duration elapsed) {
    if (perftest_result) {
      perftest_result->AppendValue(static_cast<double>(elapsed.get()));
    }

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
    return fxl::StringPrintf("%10.3lf\t%10.3lf\t%10.3lf\t%10.3lf\t%10ld", to_usecs(mean()),
                             to_usecs(first), to_usecs(best), to_usecs(worst), runs);
  }
};

// Find a suitable mixer for the provided format, channels and frame rates.
// In testing, we choose ratio-of-frame-rates and source_channels carefully, to
// trigger the selection of a specific mixer. Note: Mixers convert audio into
// our accumulation format (not the destination format), so we need not specify
// a dest_format. Actual frame rate values are unimportant, but inter-rate RATIO
// is VERY important: required SRC is the primary factor in Mix selection.
std::unique_ptr<Mixer> SelectMixer(fuchsia::media::AudioSampleFormat source_format,
                                   int32_t source_channels, int32_t source_frame_rate,
                                   int32_t dest_channels, int32_t dest_frame_rate,
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
                                                     int32_t num_channels) {
  fuchsia::media::AudioStreamType dest_details;
  dest_details.sample_format = dest_format;
  dest_details.channels = num_channels;
  dest_details.frames_per_second = 48000;

  return OutputProducer::Select(dest_details);
}

}  // namespace

bool AudioPerformance::MixerConfig::operator==(const MixerConfig& other) const {
  return sampler_type == other.sampler_type && num_input_chans == other.num_input_chans &&
         num_output_chans == other.num_output_chans && source_rate == other.source_rate &&
         dest_rate == other.dest_rate && sample_format == other.sample_format &&
         gain_type == other.gain_type && accumulate == other.accumulate;
}

bool AudioPerformance::MixerConfig::operator!=(const MixerConfig& other) const {
  return !(*this == other);
}

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

  std::string format = AsfToString(sample_format, /*abbreviate=*/true);

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

std::string AudioPerformance::MixerConfig::ToPerftestFormatForCreate() const {
  std::string sampler;
  switch (sampler_type) {
    case Resampler::SampleAndHold:
      sampler = "Point";
      break;
    case Resampler::WindowedSinc:
      sampler = "WindowedSinc";
      break;
    case Resampler::Default:
      FX_LOGS(FATAL) << "Profiler should specify the Resampler exactly";
      return "";
  }

  std::string format = AsfToString(sample_format, /*abbreviate=*/false);

  return fxl::StringPrintf("%s/%s/Channels_%d:%d/FrameRates_%06d:%06d", sampler.c_str(),
                           format.c_str(), num_input_chans, num_output_chans, source_rate,
                           dest_rate);
}

std::string AudioPerformance::MixerConfig::ToPerftestFormatForMixer() const {
  std::string gain;
  switch (gain_type) {
    case GainType::Mute:
      gain = "Mute";
      break;
    case GainType::Unity:
      gain = "Unity";
      break;
    case GainType::Scaled:
      gain = "Scaled";
      break;
    case GainType::Ramped:
      gain = "Ramped";
      break;
  }

  return fxl::StringPrintf("%s/%s%c", ToPerftestFormatForCreate().c_str(), gain.c_str(),
                           (accumulate ? '+' : '-'));
}

bool AudioPerformance::OutputProducerConfig::operator==(const OutputProducerConfig& other) const {
  return sample_format == other.sample_format && output_range == other.output_range &&
         num_chans == other.num_chans;
}

bool AudioPerformance::OutputProducerConfig::operator!=(const OutputProducerConfig& other) const {
  return !(*this == other);
}

std::string AudioPerformance::OutputProducerConfig::ToString() const {
  std::string format = AsfToString(sample_format, /*abbreviate=*/true);

  char range;
  switch (output_range) {
    case OutputSourceRange::Silence:
      range = 'S';
      break;
    case OutputSourceRange::OutOfRange:
      range = 'O';
      break;
    case OutputSourceRange::Normal:
      range = 'N';
      break;
  }

  return fxl::StringPrintf("%s-%c%u", format.c_str(), range, num_chans);
}

std::string AudioPerformance::OutputProducerConfig::ToPerftestFormat() const {
  std::string format = AsfToString(sample_format, /*abbreviate=*/false);

  std::string range;
  switch (output_range) {
    case OutputSourceRange::Silence:
      range = "Silence";
      break;
    case OutputSourceRange::OutOfRange:
      range = "OutOfRange";
      break;
    case OutputSourceRange::Normal:
      range = "Normal";
      break;
  }

  return fxl::StringPrintf("%s/%s/Channels_%u", format.c_str(), range.c_str(), num_chans);
}

void AudioPerformance::DisplayMixerCreationLegend() {
  printf("\n    Elapsed time in microseconds for a Mixer object to be created\n");
  printf(
      "\n    For mixer configuration R-fff.IO ssssss:dddddd, where:\n"
      "\t      R: Resampler type - [P]oint, [W]indowed Sinc\n"
      "\t    fff: Format - un8, i16, i24, f32\n"
      "\t      I: Input channels (one-digit number)\n"
      "\t      O: Output channels (one-digit number)\n"
      "\t ssssss: Source sample rate (six-digit integer)\n"
      "\t dddddd: Destination sample rate (six-digit integer)\n\n");
}

void AudioPerformance::DisplayMixerCreationColumnHeader() {
  printf(
      "\nCreation config        \t      Mean\t     First\t      Best\t     Worst\t  "
      "Iterations\t Mean Cached\n");
}

void AudioPerformance::ProfileMixerCreation(const std::vector<MixerConfig>& configs,
                                            const Limits& limits, perftest::ResultsSet* results) {
  auto start_time = zx::clock::get_monotonic();

  DisplayMixerCreationLegend();
  DisplayMixerCreationColumnHeader();

  for (auto& cfg : configs) {
    ProfileMixerCreation(cfg, limits, results);
  }

  DisplayMixerCreationColumnHeader();
  printf("   Total time to profile %zu Mixer creation configs: %lu ms\n   --------\n\n",
         configs.size(), (zx::clock::get_monotonic() - start_time).get() / ZX_MSEC(1));
}

void AudioPerformance::ProfileMixerCreation(const MixerConfig& cfg, const Limits& limits,
                                            perftest::ResultsSet* results) {
  auto* result = results
                     ? results->AddTestCase("fuchsia.audio.mixer_creation",
                                            cfg.ToPerftestFormatForCreate().c_str(), "nanoseconds")
                     : nullptr;
  Stats cold_cache(result);
  Stats warm_cache(result);

  // Limit to |duration_per_config| or between 5 and |runs_per_config| iterations, whichever
  // comes first.
  size_t iterations = 0;
  while (iterations < limits.min_runs_per_config ||
         (cold_cache.total < limits.duration_per_config && iterations < limits.runs_per_config)) {
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

    iterations++;
  }

  printf("%s:\t%s\t %10.3lf\n", cfg.ToStringForCreate().c_str(), cold_cache.Summary().c_str(),
         to_usecs(warm_cache.mean()));
}

void AudioPerformance::DisplayMixerLegend() {
  printf("\n    Elapsed time in microseconds for Mix() to produce %ld ms of frames\n",
         kMixLength.to_msecs());
  printf(
      "\n    For mixer configuration R-fff.IO ssssss:dddddd GA, where:\n"
      "\t      R: Resampler type - [P]oint, [W]indowed Sinc\n"
      "\t    fff: Format - un8, i16, i24, f32\n"
      "\t      I: Input channels (one-digit number)\n"
      "\t      O: Output channels (one-digit number)\n"
      "\t ssssss: Source sample rate (six-digit integer)\n"
      "\t dddddd: Destination sample rate (six-digit integer)\n\n"
      "\t      G: Gain factor - [M]ute, [U]nity, [S]caled, [R]amped\n"
      "\t      A: Accumulate - [-] no or [+] yes\n\n");
}

void AudioPerformance::DisplayMixerColumnHeader() {
  printf("Configuration             \t     Mean\t    First\t     Best\t    Worst\t  Iterations\n");
}

void AudioPerformance::ProfileMixer(const std::vector<MixerConfig>& configs, const Limits& limits,
                                    perftest::ResultsSet* results) {
  auto start_time = zx::clock::get_monotonic();

  DisplayMixerLegend();
  DisplayMixerColumnHeader();

  for (auto& cfg : configs) {
    switch (cfg.sample_format) {
      case ASF::UNSIGNED_8:
        ProfileMixer<ASF::UNSIGNED_8>(cfg, limits, results);
        break;
      case ASF::SIGNED_16:
        ProfileMixer<ASF::SIGNED_16>(cfg, limits, results);
        break;
      case ASF::SIGNED_24_IN_32:
        ProfileMixer<ASF::SIGNED_24_IN_32>(cfg, limits, results);
        break;
      case ASF::FLOAT:
        ProfileMixer<ASF::FLOAT>(cfg, limits, results);
        break;
    }
  }

  DisplayMixerColumnHeader();
  printf("   Total time to profile %zu Mixer configs: %lu ms\n   --------\n\n", configs.size(),
         (zx::clock::get_monotonic() - start_time).get() / ZX_MSEC(1));
}

template <ASF SampleFormat>
void AudioPerformance::ProfileMixer(const MixerConfig& cfg, const Limits& limits,
                                    perftest::ResultsSet* results) {
  FX_CHECK(SampleFormat == cfg.sample_format);

  double amplitude;
  if (SampleFormat == ASF::UNSIGNED_8) {
    amplitude = std::numeric_limits<int8_t>::max();
  } else if (SampleFormat == ASF::SIGNED_16) {
    amplitude = std::numeric_limits<int16_t>::max();
  } else if (SampleFormat == ASF::SIGNED_24_IN_32) {
    amplitude = std::numeric_limits<int32_t>::max() & ~0x0FF;
  } else if (SampleFormat == ASF::FLOAT) {
    amplitude = 1.0;
  } else {
    FX_LOGS(FATAL) << "Unknown sample format for mix profiling: "
                   << static_cast<int64_t>(SampleFormat);
    return;
  }

  auto mixer = SelectMixer(SampleFormat, cfg.num_input_chans, cfg.source_rate, cfg.num_output_chans,
                           cfg.dest_rate, cfg.sampler_type);
  if (mixer == nullptr) {
    return;
  }

  // Proactively construct filter tables now, so this doesn't impact mixing-time measurements.
  mixer->EagerlyPrepare();

  // Allocate enough source and destination frames for kMixLength.
  // When allocating source frames, we round up to ensure we have enough source frames.
  const int32_t dest_frame_count =
      static_cast<int32_t>(TimelineRate(cfg.dest_rate, 1'000'000'000)
                               .Scale(kMixLength.to_nsecs(), TimelineRate::RoundingMode::Floor));
  const int64_t source_frames =
      TimelineRate(cfg.source_rate, 1'000'000'000)
          .Scale(kMixLength.to_nsecs(), TimelineRate::RoundingMode::Ceiling);

  auto source_format =
      Format::Create<SampleFormat>(cfg.num_input_chans, cfg.source_rate).take_value();

  // This is a 500Hz sine wave, but the actual data doesn't matter.
  const auto periods = TimelineRate(500, 1'000'000'000).Scale(kMixLength.to_nsecs());
  auto source =
      GenerateCosineAudio(source_format, source_frames, static_cast<double>(periods), amplitude);

  auto accum = std::make_unique<float[]>(dest_frame_count * cfg.num_output_chans);
  int64_t dest_offset, previous_dest_offset;

  auto& state = mixer->state();
  state.ResetSourceStride(TimelineRate(Fixed(cfg.source_rate).raw_value(), cfg.dest_rate));

  float gain_db;
  bool source_mute = false;
  switch (cfg.gain_type) {
    case GainType::Mute:
      // 0dB, Mute
      gain_db = media_audio::kUnityGainDb;
      source_mute = true;
      break;
    case GainType::Unity:
      // 0dB
      gain_db = media_audio::kUnityGainDb;
      break;
    case GainType::Scaled:
      // -42dB
      gain_db = -42.0f;
      break;
    case GainType::Ramped:
      // -1dB => -159dB
      gain_db = media_audio::kUnityGainDb - 1.0f;
      break;
  }

  mixer->gain.SetDestGain(media_audio::kUnityGainDb);
  auto source_frames_fixed = Fixed(source_frames);

  Stats stats(results ? results->AddTestCase("fuchsia.audio.mixing",
                                             cfg.ToPerftestFormatForMixer().c_str(), "nanoseconds")
                      : nullptr);

  // Limit to |duration_per_config| or between 5 and |runs_per_config| iterations, whichever
  // comes first.
  size_t iterations = 0;
  while (iterations <= limits.min_runs_per_config ||
         (stats.total < limits.duration_per_config && iterations <= limits.runs_per_config)) {
    mixer->gain.SetSourceGain(source_mute ? fuchsia::media::audio::MUTED_GAIN_DB : gain_db);

    if (cfg.gain_type == GainType::Ramped) {
      // Ramp within the "greater than Mute but less than Unity" range. Ramp duration assumes a mix
      // duration of less than two secs.
      mixer->gain.SetSourceGainWithRamp(media_audio::kMinGainDb + 1.0f, zx::sec(2));
    }

    // For repeatability, start each run at exactly the same position.
    dest_offset = 0;
    auto source_offset = Fixed(0);
    state.set_source_pos_modulo(0);

    auto t0 = zx::clock::get_monotonic();

    while (dest_offset < dest_frame_count) {
      previous_dest_offset = dest_offset;
      mixer->Mix(accum.get(), dest_frame_count, &dest_offset, &source.samples()[0], source_frames,
                 &source_offset, cfg.accumulate);

      // Mix() might process less than all of accum, so Advance() after each.
      mixer->gain.Advance(dest_offset - previous_dest_offset,
                          TimelineRate(cfg.source_rate, ZX_SEC(1)));

      if (source_offset + mixer->pos_filter_width() >= source_frames_fixed) {
        source_offset -= source_frames_fixed;
      }
    }

    // Do *not* include the first run to avoid artificially slow initial cases.
    [[maybe_unused]] auto t1 = zx::clock::get_monotonic();
    if (iterations++ > 0) {
      stats.Add(t1 - t0);
    }
  }

  printf("%s:\t%s\n", cfg.ToStringForMixer().c_str(), stats.Summary().c_str());
}

void AudioPerformance::DisplayOutputConfigLegend() {
  printf("\n   Elapsed time in microseconds to ProduceOutput() %ld ms of frames\n",
         kMixLength.to_msecs());
  printf(
      "\n   For output configuration fff-Rn, where:\n"
      "\t   fff: Format of output data - un8, i16, i24, f32\n"
      "\t     R: Range of source data - [S]ilence, [O]ut-of-range, [N]ormal\n"
      "\t     n: Number of output channels (one-digit number)\n\n");
}

void AudioPerformance::DisplayOutputColumnHeader() {
  printf("Config\t     Mean\t    First\t     Best\t    Worst\t  Iterations\n");
}

void AudioPerformance::ProfileOutputProducer(const std::vector<OutputProducerConfig>& configs,
                                             const Limits& limits, perftest::ResultsSet* results) {
  auto start_time = zx::clock::get_monotonic();

  DisplayOutputConfigLegend();
  DisplayOutputColumnHeader();

  for (auto& cfg : configs) {
    switch (cfg.sample_format) {
      case ASF::UNSIGNED_8:
        ProfileOutputProducer<ASF::UNSIGNED_8>(cfg, limits, results);
        break;
      case ASF::SIGNED_16:
        ProfileOutputProducer<ASF::SIGNED_16>(cfg, limits, results);
        break;
      case ASF::SIGNED_24_IN_32:
        ProfileOutputProducer<ASF::SIGNED_24_IN_32>(cfg, limits, results);
        break;
      case ASF::FLOAT:
        ProfileOutputProducer<ASF::FLOAT>(cfg, limits, results);
        break;
    }
  }

  DisplayOutputColumnHeader();
  printf("   Total time to profile %zu OutputProducer configs: %lu ms\n   --------\n\n",
         configs.size(), (zx::clock::get_monotonic() - start_time).get() / ZX_MSEC(1));
}

template <ASF SampleFormat>
void AudioPerformance::ProfileOutputProducer(const OutputProducerConfig& cfg, const Limits& limits,
                                             perftest::ResultsSet* results) {
  FX_CHECK(SampleFormat == cfg.sample_format);

  auto output_producer = SelectOutputProducer(SampleFormat, cfg.num_chans);
  if (!output_producer) {
    return;
  }

  // Produce 10ms worth of output at 48kHz.
  using SampleT = typename SampleFormatTraits<SampleFormat>::SampleT;
  int32_t frame_count =
      static_cast<int32_t>(TimelineRate(48000, 1'000'000'000).Scale(kMixLength.to_nsecs()));
  int32_t num_samples = frame_count * cfg.num_chans;
  auto dest = std::make_unique<SampleT[]>(num_samples);

  Stats stats(results ? results->AddTestCase("fuchsia.audio.mixer_output",
                                             cfg.ToPerftestFormat().c_str(), "nanoseconds")
                      : nullptr);

  if (cfg.output_range == OutputSourceRange::Silence) {
    // Limit to |duration_per_config| or between 5 and |runs_per_config| iterations, whichever
    // comes first.
    size_t iterations = 0;
    while (iterations <= limits.min_runs_per_config ||
           (stats.total < limits.duration_per_config && iterations <= limits.runs_per_config)) {
      auto t0 = zx::clock::get_monotonic();
      output_producer->FillWithSilence(dest.get(), frame_count);

      // Do *not* include the first run to avoid artificially slow initial cases.
      [[maybe_unused]] auto t1 = zx::clock::get_monotonic();
      if (iterations++ > 0) {
        stats.Add(t1 - t0);
      }
    }
  } else {
    auto accum_format = Format::Create<ASF::FLOAT>(cfg.num_chans, 48000 /* unused */).take_value();
    AudioBuffer accum(accum_format, 0);

    switch (cfg.output_range) {
      case OutputSourceRange::OutOfRange:
        accum = AudioBuffer(accum_format, frame_count);
        for (int32_t idx = 0; idx < num_samples; ++idx) {
          accum.samples()[idx] = (idx % 2 ? -1.5f : 1.5f);
        }
        break;
      case OutputSourceRange::Normal: {
        // This is a 1kHz sine wave, but the actual shape doesn't matter.
        // We use an amplitude < 1.0 to avoid code that clamps +1.0 values on integer outputs.
        const auto periods = TimelineRate(1000, 1'000'000'000).Scale(kMixLength.to_nsecs());
        accum = GenerateCosineAudio(accum_format, frame_count, static_cast<double>(periods), 0.9);
        break;
      }
      default:
        FX_LOGS(FATAL) << "Unknown output range: " << static_cast<int64_t>(cfg.output_range);
        return;
    }

    // Limit to |duration_per_config| or between 5 and |runs_per_config| iterations, whichever
    // comes first.
    size_t iterations = 0;
    while (iterations <= limits.min_runs_per_config ||
           (stats.total < limits.duration_per_config && iterations <= limits.runs_per_config)) {
      auto t0 = zx::clock::get_monotonic();
      output_producer->ProduceOutput(&accum.samples()[0], dest.get(), frame_count);

      // Do *not* include the first run to avoid artificially slow initial cases.
      [[maybe_unused]] auto t1 = zx::clock::get_monotonic();
      if (iterations++ > 0) {
        stats.Add(t1 - t0);
      }
    }
  }

  printf("%s:\t%s\n", cfg.ToString().c_str(), stats.Summary().c_str());
}

}  // namespace media::audio::tools

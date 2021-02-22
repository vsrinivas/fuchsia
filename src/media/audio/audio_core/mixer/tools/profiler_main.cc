// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <map>
#include <set>

#include "gperftools/profiler.h"
#include "lib/syslog/cpp/log_settings.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/media/audio/audio_core/mixer/tools/audio_performance.h"

using Resampler = ::media::audio::Mixer::Resampler;
using ASF = fuchsia::media::AudioSampleFormat;
using AudioPerformance = media::audio::tools::AudioPerformance;
using GainType = AudioPerformance::GainType;
using InputRange = AudioPerformance::InputRange;
using MixerConfig = AudioPerformance::MixerConfig;
using OutputProducerConfig = AudioPerformance::OutputProducerConfig;

namespace {

enum class Benchmark { Create, Mix, Output };

struct Options {
  zx::duration duration_per_config;

  std::set<Benchmark> enabled;
  bool enable_pprof;

  // MixerConfig + OutputProducerConfig.
  std::set<ASF> sample_formats;
  std::set<std::pair<uint32_t, uint32_t>> num_input_output_chans;

  // MixerConfig.
  std::set<Resampler> samplers;
  std::set<std::pair<uint32_t, uint32_t>> source_dest_rates;
  std::set<GainType> gain_types;
  std::set<bool> accumulates;

  // OutputProducerConfig.
  std::set<InputRange> input_ranges;
};

std::vector<MixerConfig> ConfigsForMixerCreation(const Options& opt) {
  if (opt.enabled.count(Benchmark::Create) == 0) {
    return {};
  }
  if (opt.samplers.count(Resampler::WindowedSinc) == 0) {
    return {};
  }

  std::vector<MixerConfig> out;
  for (auto [source_rate, dest_rate] : opt.source_dest_rates) {
    out.push_back({
        .sampler_type = Resampler::WindowedSinc,
        .num_input_chans = 1,   // this has no effect on mixer creation time
        .num_output_chans = 1,  // this has no effect on mixer creation time
        .source_rate = source_rate,
        .dest_rate = dest_rate,
        .sample_format = ASF::FLOAT,  // this has no effect on mixer creation time
    });
  }

  return out;
}

std::vector<MixerConfig> ConfigsForMixer(const Options& opt) {
  if (opt.enabled.count(Benchmark::Mix) == 0) {
    return {};
  }

  std::vector<MixerConfig> out;

  for (auto sampler : opt.samplers) {
    for (auto [source_rate, dest_rate] : opt.source_dest_rates) {
      if (sampler == Resampler::SampleAndHold && source_rate != dest_rate) {
        continue;
      }
      for (auto [num_input_chans, num_output_chans] : opt.num_input_output_chans) {
        for (auto sample_format : opt.sample_formats) {
          for (auto gain_type : opt.gain_types) {
            for (auto accumulate : opt.accumulates) {
              out.push_back({
                  .sampler_type = sampler,
                  .num_input_chans = num_input_chans,
                  .num_output_chans = num_output_chans,
                  .source_rate = source_rate,
                  .dest_rate = dest_rate,
                  .sample_format = sample_format,
                  .gain_type = gain_type,
                  .accumulate = accumulate,
              });
            }
          }
        }
      }
    }
  }

  return out;
}

std::vector<OutputProducerConfig> ConfigsForOutputProducer(const Options& opt) {
  if (opt.enabled.count(Benchmark::Output) == 0) {
    return {};
  }

  std::vector<OutputProducerConfig> out;

  for (auto [num_input_chans, num_output_chans] : opt.num_input_output_chans) {
    for (auto sample_format : opt.sample_formats) {
      for (auto input_range : opt.input_ranges) {
        out.push_back({
            .sample_format = sample_format,
            .input_range = input_range,
            .num_chans = num_output_chans,
        });
      }
    }
  }

  return out;
}

const Options kDefaultOpts = {
    // Expected run time for kDefaultOpts is about 4.5 minutes on an astro device.
    .duration_per_config = zx::msec(250),
    .enabled = {Benchmark::Create, Benchmark::Mix, Benchmark::Output},
    .enable_pprof = false,
    .sample_formats =
        {
            // skip ASF::UNSIGNED_8: that is rarely used
            ASF::SIGNED_16,
            ASF::SIGNED_24_IN_32,
            ASF::FLOAT,
        },
    .num_input_output_chans =
        {
            {1, 1},
            {1, 2},
            {2, 1},
            {2, 2},
            {4, 4},
        },
    .samplers = {Resampler::SampleAndHold, Resampler::WindowedSinc},
    .source_dest_rates =
        {
            // Typical capture paths
            {96000, 16000},
            {96000, 48000},
            // Typical render paths
            {48000, 48000},
            {44100, 48000},
            {48000, 96000},
            // Extreme cases
            {8000, 192000},
            // TODO(fxbug.dev/37356): re-enable after the Mixer transitions from 32->64 bit offsets
            // Note: the expected 12 minute runtime assumes this is enabled
            //{192000, 8000},
        },
    .gain_types = {GainType::Mute, GainType::Unity, GainType::Scaled, GainType::Ramped},
    .accumulates = {false, true},
    .input_ranges = {InputRange::Silence, InputRange::OutOfRange, InputRange::Normal},
};

void Usage(const char* prog_name) {
  printf("\nUsage: %s [--option] [...]\n", prog_name);
  printf("Measure the performance of the audio mixer in microbenchmark operations.\n");
  printf("\n");
  printf("By default, all types of benchmarks are enabled using a default\n");
  printf("set of configurations. Valid options are:\n");
  printf("\n");
  printf("  --bench-time=<seconds>\n");
  printf("    Each benchmark is run for at least this long. Defaults to 0.5s.\n");
  printf("\n");
  printf("  --enable-create=<bool>\n");
  printf("    Enable Mixer creation benchmarks (default=true).\n");
  printf("  --enable-mixing=<bool>\n");
  printf("    Enable Mixer::Mix() benchmarks (default=true).\n");
  printf("  --enable-output-producer=<bool>\n");
  printf("    Enable OutputProducer benchmarks (default=true).\n");
  printf("\n");
  printf("  --enable-pprof=<bool>\n");
  printf("    Dump a pprof-compatible profile to /tmp/audio_mixer_profiler.pprof.\n");
  printf("    Defaults to false.\n");
  printf("\n");
  printf("  --samplers=[point|sinc]*\n");
  printf("    Enable these samplers. Multiple samplers can be separated by commas.\n");
  printf("    For example: --samplers=point,sinc\n");
  printf("\n");
  printf("  --channels=[input_chans:output_chans]*\n");
  printf("    Enable these channel configs. Multiple configs can be separated by commas.\n");
  printf("    For example: --channels=1:2,1:4\n");
  printf("\n");
  printf("  --frame-rates=[source_rate:dest_rate]*\n");
  printf("    Enable these frame rate configs. Multiple configs can be separated by commas.\n");
  printf("    For example: --frame-rates=48000:48000,16000:48000\n");
  printf("\n");
  printf("  --sample-formats=[uint8|int16|int24|float]*\n");
  printf("    Enable these sample formats. Multiple sample formats can be separated by commas.\n");
  printf("\n");
  printf("  --mixer-gains=[mute|unity|scaled|ramped]*\n");
  printf("    Enable these mixer gain configs. Multiple configs can be separated by commas.\n");
  printf("\n");
  printf("  --output-producer-input-types=[silence|out-of-range|normal]*\n");
  printf("    Enable these kinds of inputs for OutputProducer benchmarks. Multiple kinds of\n");
  printf("    inputs can be separated by commas.\n");
  printf("\n");
}

Options ParseCommandLine(int argc, char** argv) {
  auto opt = kDefaultOpts;
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  auto bool_flag = [&command_line](const std::string& flag_name, bool& out) {
    if (!command_line.HasOption(flag_name)) {
      return;
    }
    std::string str;
    command_line.GetOptionValue(flag_name, &str);
    if (str == "" || str == "true") {
      out = true;
    } else {
      out = false;
    }
  };

  auto duration_seconds_flag = [&command_line](const std::string& flag_name, zx::duration& out) {
    if (!command_line.HasOption(flag_name)) {
      return;
    }
    std::string str;
    command_line.GetOptionValue(flag_name, &str);
    double d = std::stod(str);
    out = zx::nsec(static_cast<int64_t>(d * 1e9));
  };

  auto enum_flagset = [&command_line](const std::string& flag_name, auto& out, auto value_mapping) {
    if (!command_line.HasOption(flag_name)) {
      return;
    }
    out.clear();
    std::string str;
    command_line.GetOptionValue(flag_name, &str);
    for (auto s : fxl::SplitStringCopy(str, ",", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty)) {
      if (value_mapping.count(s) > 0) {
        out.insert(value_mapping[s]);
      }
    }
  };

  auto uint32_pair_flagset = [&command_line](const std::string& flag_name,
                                             std::set<std::pair<uint32_t, uint32_t>>& out) {
    if (!command_line.HasOption(flag_name)) {
      return;
    }
    out.clear();
    std::string str;
    command_line.GetOptionValue(flag_name, &str);
    for (auto s : fxl::SplitStringCopy(str, ",", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty)) {
      auto pair = fxl::SplitStringCopy(s, ":", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
      if (pair.size() == 2) {
        out.insert(
            {static_cast<uint32_t>(std::stoi(pair[0])), static_cast<uint32_t>(std::stoi(pair[1]))});
      }
    }
  };

  if (command_line.HasOption("help")) {
    Usage(argv[0]);
    exit(0);
  }

  duration_seconds_flag("bench-time", opt.duration_per_config);

  bool enable_create = true;
  bool enable_mixing = true;
  bool enable_output_producer = true;
  bool_flag("enable-create", enable_create);
  bool_flag("enable-mixing", enable_mixing);
  bool_flag("enable-output-producer", enable_output_producer);

  if (!enable_create) {
    opt.enabled.erase(Benchmark::Create);
  }
  if (!enable_mixing) {
    opt.enabled.erase(Benchmark::Mix);
  }
  if (!enable_output_producer) {
    opt.enabled.erase(Benchmark::Output);
  }

  bool_flag("enable-pprof", opt.enable_pprof);

  enum_flagset("samplers", opt.samplers,
               std::map<std::string, Resampler>{
                   {"point", Resampler::SampleAndHold},
                   {"sinc", Resampler::WindowedSinc},
               });

  uint32_pair_flagset("channels", opt.num_input_output_chans);
  uint32_pair_flagset("frame-rates", opt.source_dest_rates);

  enum_flagset("sample-formats", opt.sample_formats,
               std::map<std::string, ASF>{
                   {"uint8", ASF::UNSIGNED_8},
                   {"int16", ASF::SIGNED_16},
                   {"int32", ASF::SIGNED_24_IN_32},
                   {"float", ASF::FLOAT},
               });

  enum_flagset("mixer-gains", opt.gain_types,
               std::map<std::string, GainType>{
                   {"mute", GainType::Mute},
                   {"unity", GainType::Unity},
                   {"scaled", GainType::Scaled},
                   {"ramped", GainType::Ramped},
               });

  enum_flagset("output-producer-input-types", opt.input_ranges,
               std::map<std::string, InputRange>{
                   {"silence", InputRange::Silence},
                   {"out-of-range", InputRange::OutOfRange},
                   {"normal", InputRange::Normal},
               });

  return opt;
}

}  // namespace

int main(int argc, char** argv) {
  syslog::SetTags({"audio_mixer_profiler"});

  auto opt = ParseCommandLine(argc, argv);
  printf("\n\n Performance Profiling\n\n");

  if (opt.enable_pprof) {
    ProfilerStart("/tmp/audio_mixer_profiler.pprof");
  }
  if (opt.enabled.count(Benchmark::Create) > 0) {
    AudioPerformance::ProfileMixerCreation(ConfigsForMixerCreation(opt), opt.duration_per_config);
  }

  if (opt.enabled.count(Benchmark::Mix) > 0) {
    AudioPerformance::ProfileMixer(ConfigsForMixer(opt), opt.duration_per_config);
  }

  if (opt.enabled.count(Benchmark::Output) > 0) {
    AudioPerformance::ProfileOutputProducer(ConfigsForOutputProducer(opt), opt.duration_per_config);
  }

  if (opt.enable_pprof) {
    ProfilerStop();
  }
  return 0;
}

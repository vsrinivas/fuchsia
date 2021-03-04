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
  // Duration and iteration limits per config.
  AudioPerformance::Limits limits;

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

  // JSON filepath to export perftest results.
  std::optional<std::string> perftest_json;

  // Provide matching source and dest rates if available; else, return a default.
  const std::pair<uint32_t, uint32_t> matching_rates() const {
    for (auto [src, dest] : source_dest_rates) {
      if (src == dest) {
        return {src, dest};
      }
    }
    return {48000, 48000};
  }
};

constexpr char kBenchmarkDurationSwitch[] = "bench-time";
constexpr char kBenchmarkRunsSwitch[] = "bench-runs";

constexpr char kProfileMixerCreationSwitch[] = "enable-create";
constexpr char kProfileMixingSwitch[] = "enable-mix";
constexpr char kProfileOutputSwitch[] = "enable-output";

constexpr char kEnablePprofSwitch[] = "enable-pprof";

constexpr char kSamplerSwitch[] = "samplers";
constexpr char kSamplerPointOption[] = "point";
constexpr char kSamplerSincOption[] = "sinc";

constexpr char kChannelsSwitch[] = "channels";

constexpr char kFrameRatesSwitch[] = "frame-rates";

constexpr char kSampleFormatsSwitch[] = "sample-formats";
constexpr char kSampleFormatUint8Option[] = "uint8";
constexpr char kSampleFormatInt16Option[] = "int16";
constexpr char kSampleFormatInt24In32Option[] = "int24";
constexpr char kSampleFormatFloat32Option[] = "float";

constexpr char kMixingGainsSwitch[] = "mix-gains";
constexpr char kMixingGainMuteOption[] = "mute";
constexpr char kMixingGainUnityOption[] = "unity";
constexpr char kMixingGainScaledOption[] = "scaled";
constexpr char kMixingGainRampedOption[] = "ramped";

constexpr char kOutputProducerSourceRangesSwitch[] = "output-ranges";
constexpr char kOutputProducerSourceRangeSilenceOption[] = "silence";
constexpr char kOutputProducerSourceRangeOutOfRangeOption[] = "out-of-range";
constexpr char kOutputProducerSourceRangeNormalOption[] = "normal";

constexpr char kPerftestJsonFilepathSwitch[] = "perftest-json";

constexpr char kUsageSwitch[] = "help";

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

// Create mixer configs that cover every combination of provided Options.
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

// Create mixer configs such that one of each provided Option is included in a config.
std::vector<MixerConfig> ConfigsForMixerReduced(const Options& opt) {
  if (opt.enabled.count(Benchmark::Mix) == 0) {
    return {};
  }

  std::vector<MixerConfig> out;

  for (auto sampler : opt.samplers) {
    // Create base config from which to deviate. Note: point sampler can only accept matching source
    // and dest rates, so we accommodate that here.
    MixerConfig base_config = {
        .sampler_type = sampler,
        .num_input_chans = opt.num_input_output_chans.begin()->first,
        .num_output_chans = opt.num_input_output_chans.begin()->second,
        .source_rate = sampler == Resampler::SampleAndHold ? opt.matching_rates().first
                                                           : opt.source_dest_rates.begin()->first,
        .dest_rate = sampler == Resampler::SampleAndHold ? opt.matching_rates().second
                                                         : opt.source_dest_rates.begin()->second,
        .sample_format = *opt.sample_formats.begin(),
        .gain_type = *opt.gain_types.begin(),
        .accumulate = *opt.accumulates.begin(),
    };
    out.push_back(base_config);

    for (auto [source_rate, dest_rate] : opt.source_dest_rates) {
      if (sampler == Resampler::SampleAndHold && source_rate != dest_rate) {
        continue;
      }
      MixerConfig config = base_config;
      config.sampler_type = sampler;
      config.source_rate = source_rate;
      config.dest_rate = dest_rate;
      if (config != base_config) {
        out.push_back(config);
      }
    }
    for (auto [num_input_chans, num_output_chans] : opt.num_input_output_chans) {
      MixerConfig config = base_config;
      config.num_input_chans = num_input_chans;
      config.num_output_chans = num_output_chans;
      if (config != base_config) {
        out.push_back(config);
      }
    }
    for (auto sample_format : opt.sample_formats) {
      MixerConfig config = base_config;
      config.sample_format = sample_format;
      if (config != base_config) {
        out.push_back(config);
      }
    }
    for (auto gain_type : opt.gain_types) {
      MixerConfig config = base_config;
      config.gain_type = gain_type;
      if (config != base_config) {
        out.push_back(config);
      }
    }
    for (auto accumulate : opt.accumulates) {
      MixerConfig config = base_config;
      config.accumulate = accumulate;
      if (config != base_config) {
        out.push_back(config);
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

// Create output producer configs such that one of each provided Option is included in a config.
std::vector<OutputProducerConfig> ConfigsForOutputProducerReduced(const Options& opt) {
  if (opt.enabled.count(Benchmark::Output) == 0) {
    return {};
  }

  std::vector<OutputProducerConfig> out;

  OutputProducerConfig base_config = {.sample_format = *opt.sample_formats.begin(),
                                      .input_range = *opt.input_ranges.begin(),
                                      .num_chans = opt.num_input_output_chans.begin()->second};

  for (auto [num_input_chans, num_output_chans] : opt.num_input_output_chans) {
    OutputProducerConfig config = base_config;
    config.num_chans = num_output_chans;
    if (config != base_config) {
      out.push_back(config);
    }
  }
  for (auto sample_format : opt.sample_formats) {
    OutputProducerConfig config = base_config;
    config.sample_format = sample_format;
    if (config != base_config) {
      out.push_back(config);
    }
  }
  for (auto input_range : opt.input_ranges) {
    OutputProducerConfig config = base_config;
    config.input_range = input_range;
    if (config != base_config) {
      out.push_back(config);
    }
  }

  return out;
}

const Options kDefaultOpts = {
    // Expected run time for kDefaultOpts is ~1m40s for a full run and ~2s for a reduced (perftest)
    // run on an astro device.
    .limits = {.duration_per_config = zx::msec(250), .runs_per_config = 100},
    .enabled = {Benchmark::Create, Benchmark::Mix, Benchmark::Output},
    .enable_pprof = false,
    .sample_formats =
        {
            ASF::UNSIGNED_8,
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
            {192000, 8000},
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
  printf("  --%s=<seconds>\n", kBenchmarkDurationSwitch);
  printf(
      "    Each benchmark is run for at least this long or --%s, whichever comes first. "
      "Defaults to 0.25s.\n",
      kBenchmarkRunsSwitch);
  printf("\n");
  printf("  --%s=<runs>\n", kBenchmarkRunsSwitch);
  printf(
      "    Each benchmark is run for at least this many iterations or --%s, whichever "
      "comes first. Defaults to 100 runs.\n",
      kBenchmarkDurationSwitch);
  printf("\n");
  printf("  --%s=<bool>\n", kProfileMixerCreationSwitch);
  printf("    Enable Mixer creation benchmarks (default=true).\n");
  printf("  --%s=<bool>\n", kProfileMixingSwitch);
  printf("    Enable Mixer::Mix() benchmarks (default=true).\n");
  printf("  --%s=<bool>\n", kProfileOutputSwitch);
  printf("    Enable OutputProducer benchmarks (default=true).\n");
  printf("\n");
  printf("  --%s=<bool>\n", kEnablePprofSwitch);
  printf("    Dump a pprof-compatible profile to /tmp/audio_mixer_profiler.pprof.\n");
  printf("    Defaults to false.\n");
  printf("\n");
  printf("  --%s=[%s|%s]*\n", kSamplerSwitch, kSamplerPointOption, kSamplerSincOption);
  printf("    Enable these samplers. Multiple samplers can be separated by commas.\n");
  printf("    For example: --%s=%s,%s\n", kSamplerSwitch, kSamplerPointOption, kSamplerSincOption);
  printf("\n");
  printf("  --%s=[input_chans:output_chans]*\n", kChannelsSwitch);
  printf("    Enable these channel configs. Multiple configs can be separated by commas.\n");
  printf("    For example: --%s=1:2,1:4\n", kChannelsSwitch);
  printf("\n");
  printf("  --%s=[source_rate:dest_rate]*\n", kFrameRatesSwitch);
  printf("    Enable these frame rate configs. Multiple configs can be separated by commas.\n");
  printf("    For example: --%s=48000:48000,16000:48000\n", kFrameRatesSwitch);
  printf("\n");
  printf("  --%s=[%s|%s|%s|%s]*\n", kSampleFormatsSwitch, kSampleFormatUint8Option,
         kSampleFormatInt16Option, kSampleFormatInt24In32Option, kSampleFormatFloat32Option);
  printf("    Enable these sample formats. Multiple sample formats can be separated by commas.\n");
  printf("\n");
  printf("  --%s=[%s|%s|%s|%s]*\n", kMixingGainsSwitch, kMixingGainMuteOption,
         kMixingGainUnityOption, kMixingGainScaledOption, kMixingGainRampedOption);
  printf("    Enable these mixer gain configs. Multiple configs can be separated by commas.\n");
  printf("\n");
  printf("  --%s=[%s|%s|%s]*\n", kOutputProducerSourceRangesSwitch,
         kOutputProducerSourceRangeSilenceOption, kOutputProducerSourceRangeOutOfRangeOption,
         kOutputProducerSourceRangeNormalOption);
  printf("    Enable these kinds of inputs for OutputProducer benchmarks. Multiple kinds of\n");
  printf("    inputs can be separated by commas.\n");
  printf("\n");
  printf("  --%s=<filepath.json>\n", kPerftestJsonFilepathSwitch);
  printf("    Include a json filepath to record perftest results.\n");
  printf("\n");
  printf("  --%s\n", kUsageSwitch);
  printf("    Display this message.\n");
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

  if (command_line.HasOption(kUsageSwitch)) {
    Usage(argv[0]);
    exit(0);
  }

  if (command_line.HasOption(kPerftestJsonFilepathSwitch)) {
    std::string json;
    command_line.GetOptionValue(kPerftestJsonFilepathSwitch, &json);
    opt.perftest_json = json;
  }

  if (command_line.HasOption(kBenchmarkRunsSwitch)) {
    std::string runs;
    command_line.GetOptionValue(kBenchmarkRunsSwitch, &runs);
    opt.limits.runs_per_config = std::stoi(runs);
  }

  duration_seconds_flag(kBenchmarkDurationSwitch, opt.limits.duration_per_config);

  bool profile_creation = true;
  bool profile_mixing = true;
  bool profile_output_producer = true;
  bool_flag(kProfileMixerCreationSwitch, profile_creation);
  bool_flag(kProfileMixingSwitch, profile_mixing);
  bool_flag(kProfileOutputSwitch, profile_output_producer);

  if (!profile_creation) {
    opt.enabled.erase(Benchmark::Create);
  }
  if (!profile_mixing) {
    opt.enabled.erase(Benchmark::Mix);
  }
  if (!profile_output_producer) {
    opt.enabled.erase(Benchmark::Output);
  }

  bool_flag(kEnablePprofSwitch, opt.enable_pprof);

  enum_flagset(kSamplerSwitch, opt.samplers,
               std::map<std::string, Resampler>{
                   {kSamplerPointOption, Resampler::SampleAndHold},
                   {kSamplerSincOption, Resampler::WindowedSinc},
               });

  uint32_pair_flagset(kChannelsSwitch, opt.num_input_output_chans);
  uint32_pair_flagset(kFrameRatesSwitch, opt.source_dest_rates);

  enum_flagset(kSampleFormatsSwitch, opt.sample_formats,
               std::map<std::string, ASF>{
                   {kSampleFormatUint8Option, ASF::UNSIGNED_8},
                   {kSampleFormatInt16Option, ASF::SIGNED_16},
                   {kSampleFormatInt24In32Option, ASF::SIGNED_24_IN_32},
                   {kSampleFormatFloat32Option, ASF::FLOAT},
               });

  enum_flagset(kMixingGainsSwitch, opt.gain_types,
               std::map<std::string, GainType>{
                   {kMixingGainMuteOption, GainType::Mute},
                   {kMixingGainUnityOption, GainType::Unity},
                   {kMixingGainScaledOption, GainType::Scaled},
                   {kMixingGainRampedOption, GainType::Ramped},
               });

  enum_flagset(kOutputProducerSourceRangesSwitch, opt.input_ranges,
               std::map<std::string, InputRange>{
                   {kOutputProducerSourceRangeSilenceOption, InputRange::Silence},
                   {kOutputProducerSourceRangeOutOfRangeOption, InputRange::OutOfRange},
                   {kOutputProducerSourceRangeNormalOption, InputRange::Normal},
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

  perftest::ResultsSet* results = nullptr;
  if (opt.perftest_json) {
    results = new perftest::ResultsSet();
  }

  if (opt.enabled.count(Benchmark::Create) > 0) {
    AudioPerformance::ProfileMixerCreation(ConfigsForMixerCreation(opt), opt.limits, results);
  }

  if (opt.enabled.count(Benchmark::Mix) > 0) {
    AudioPerformance::ProfileMixer(results ? ConfigsForMixerReduced(opt) : ConfigsForMixer(opt),
                                   opt.limits, results);
  }

  if (opt.enabled.count(Benchmark::Output) > 0) {
    AudioPerformance::ProfileOutputProducer(
        results ? ConfigsForOutputProducerReduced(opt) : ConfigsForOutputProducer(opt), opt.limits,
        results);
  }

  if (opt.enable_pprof) {
    ProfilerStop();
  }

  if (results) {
    return results->WriteJSONFile(opt.perftest_json.value().c_str()) ? 0 : 1;
  }

  return 0;
}

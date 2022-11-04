// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/scheduler/cpp/fidl.h>
#include <lib/syslog/cpp/log_settings.h>

#include <vector>

#include "gperftools/profiler.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/strings/split_string.h"
#include "src/media/audio/audio_core/tools/output_pipeline_benchmark/output_pipeline_benchmark.h"
#include "src/media/audio/audio_core/v1/mix_profile_config.h"
#include "src/media/audio/audio_core/v1/pin_executable_memory.h"
#include "src/media/audio/audio_core/v1/stream_usage.h"

using media::audio::MixProfileConfig;
using media::audio::OutputPipelineBenchmark;

namespace {

struct Options {
  std::vector<OutputPipelineBenchmark::Scenario> scenarios;
  int64_t runs_per_scenario;
  zx::duration mix_period;
  std::optional<std::string> perftest_json;
  bool enable_pprof = false;
  bool enable_deadline_profile = false;
  bool hide_legend = false;
};

std::string ToString(const std::vector<OutputPipelineBenchmark::Scenario>& scenarios) {
  std::string out;
  std::string sep;
  for (auto& s : scenarios) {
    out += sep + s.ToString();
    sep = ",";
  }
  return out;
}

const Options kDefaultOptions = {
    // Default to M, C, and U, separate and together.
    .scenarios =
        {
            OutputPipelineBenchmark::Scenario::FromString("empty"),
            OutputPipelineBenchmark::Scenario::FromString("M/VC"),
            OutputPipelineBenchmark::Scenario::FromString("C/VC"),
            OutputPipelineBenchmark::Scenario::FromString("U/VC"),
            OutputPipelineBenchmark::Scenario::FromString("MCU/VM"),
            OutputPipelineBenchmark::Scenario::FromString("MCU/VC"),
            OutputPipelineBenchmark::Scenario::FromString("MCU/VS"),
            OutputPipelineBenchmark::Scenario::FromString("MCU/VR"),
        },

    // Default to 10ms mix periods run 100x, for a total of 1s per scenario.
    .runs_per_scenario = 100,
    .mix_period = zx::msec(10),
};

void Usage(const char* prog_name) {
  printf("\nUsage: %s [--option] [...]\n", prog_name);
  printf("Measure the performance of an audio output pipeline.\n");
  printf("\n");
  printf("Valid options are:\n");
  printf("\n");
  printf("  --scenarios=<string>\n");
  printf("    Run these scenarios. Comma-separated list of scenarios. For example,\n");
  printf("    \"MC/VC,U/VR\" contains two scenarios: the first has MEDIA and COMMUNICATION\n");
  printf("    inputs and constant volume, while the second has an ULTRASOUND input and\n");
  printf("    ramped volume. The special string \"empty\" runs the pipeline with no input\n");
  printf("    streams. Defaults to: %s\n", ToString(kDefaultOptions.scenarios).c_str());
  printf("\n");
  printf("  --runs-per-scenario=<count>\n");
  printf("    Run each scenario this many times (default: %ld).\n",
         kDefaultOptions.runs_per_scenario);
  printf("\n");
  printf("  --mix-period=<seconds>\n");
  printf("    Length of each mix job (default: %.2f sec).\n",
         static_cast<double>(kDefaultOptions.mix_period.to_msecs()) / 1000.0);
  printf("\n");
  printf("  --perftest-json=<filepath.json>\n");
  printf("    Record perftest results to the specified json filepath.\n");
  printf("\n");
  printf("  --enable-pprof=<bool>\n");
  printf("    Save a pprof-compatible log to /tmp/%s.pprof (default: false).\n", prog_name);
  printf("\n");
  printf("  --enable-deadline-profile=<bool>\n");
  printf(
      "    If true, try to apply a deadline profile to the benchmark thread. (default: false).\n");
  printf("\n");
  printf("  --hide-legend\n");
  printf("    Don't display a verbose explanation of scenario types and other details.\n");
  printf("\n");
  printf("  --help\n");
  printf("    Display this message.\n");
  printf("\n");
}

Options ParseCommandLine(int argc, char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  Options opts = kDefaultOptions;

  if (command_line.HasOption("help")) {
    Usage(argv[0]);
    exit(0);
  }

  if (command_line.HasOption("scenarios")) {
    std::string str;
    command_line.GetOptionValue("scenarios", &str);
    opts.scenarios.clear();
    for (auto s : fxl::SplitStringCopy(str, ",", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty)) {
      FX_CHECK(s.size() > 0);
      opts.scenarios.push_back(OutputPipelineBenchmark::Scenario::FromString(s));
    }
  }

  if (command_line.HasOption("runs-per-scenario")) {
    std::string str;
    command_line.GetOptionValue("runs-per-scenario", &str);
    opts.runs_per_scenario = std::stol(str);
  }

  if (command_line.HasOption("mix-period")) {
    std::string str;
    command_line.GetOptionValue("mix-period", &str);
    double d = std::stod(str);
    opts.mix_period = zx::nsec(static_cast<int64_t>(d * 1e9));
  }

  if (command_line.HasOption("enable-pprof")) {
    std::string str;
    command_line.GetOptionValue("enable-pprof", &str);
    if (str == "" || str == "true") {
      opts.enable_pprof = true;
    } else {
      opts.enable_pprof = false;
    }
  }

  if (command_line.HasOption("enable-deadline-profile")) {
    std::string str;
    command_line.GetOptionValue("enable-deadline-profile", &str);
    if (str == "" || str == "true") {
      opts.enable_deadline_profile = true;
    } else {
      opts.enable_deadline_profile = false;
    }
  }

  if (command_line.HasOption("perftest-json")) {
    std::string json;
    command_line.GetOptionValue("perftest-json", &json);
    opts.perftest_json = json;
  }

  opts.hide_legend = command_line.HasOption("hide-legend");

  return opts;
}

void RegisterDeadlineProfile(sys::ComponentContext& context,
                             const MixProfileConfig& mix_profile_config,
                             const std::string& prog_name) {
  fuchsia::scheduler::ProfileProviderSyncPtr profile_provider;
  if (auto status = context.svc()->Connect(profile_provider.NewRequest()); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "could not connect to profile provider";
  }

  zx::thread thread;
  if (auto status = zx::thread::self()->duplicate(ZX_RIGHT_SAME_RIGHTS, &thread); status != ZX_OK) {
    FX_PLOGS(FATAL, status) << "could not dup thread handle";
  }

  zx::profile profile;
  zx_status_t profile_status;
  zx_status_t fidl_status = profile_provider->GetDeadlineProfile(
      mix_profile_config.capacity.get(), mix_profile_config.deadline.get(),
      mix_profile_config.period.get(), "audio_core_output_pipeline_benchmark", &profile_status,
      &profile);
  if (fidl_status != ZX_OK) {
    FX_PLOGS(FATAL, fidl_status) << "could not call GetDeadlineProfile";
  }
  if (profile_status != ZX_OK) {
    FX_PLOGS(FATAL, profile_status)
        << "GetDeadlineProfile request (capacity=" << mix_profile_config.capacity.get()
        << " deadline=" << mix_profile_config.deadline.get()
        << " period=" << mix_profile_config.period.get() << ") rejected";
  }
}

}  // namespace

int main(int argc, char** argv) {
  // Rewrite the program name to be just the executable file, not the full path.
  const std::string prog_name = files::GetBaseName(argv[0]);
  syslog::SetTags({prog_name});

  auto opts = ParseCommandLine(argc, argv);
  printf("Audio output pipeline profiling tool\n");

  auto context = sys::ComponentContext::Create();

  // Prime the output pipeline so the first mix is not artificially slow.
  media::audio::PinExecutableMemory::Singleton();
  OutputPipelineBenchmark benchmark(*context);

  if (opts.enable_deadline_profile) {
    RegisterDeadlineProfile(*context, benchmark.process_config().mix_profile_config(), prog_name);
  }

  benchmark.Run(OutputPipelineBenchmark::Scenario::FromString("BMISCU/VC"), 1, zx::msec(10),
                nullptr, false);

  std::string pprof_file = fxl::StringPrintf("/tmp/%s.pprof", prog_name.c_str());
  if (opts.enable_pprof) {
    ProfilerStart(pprof_file.c_str());
  }

  perftest::ResultsSet* results = nullptr;
  if (opts.perftest_json) {
    results = new perftest::ResultsSet();
  }

  if (!opts.hide_legend) {
    benchmark.PrintLegend(opts.mix_period);
  }

  benchmark.PrintHeader();
  for (auto& scenario : opts.scenarios) {
    benchmark.Run(scenario, opts.runs_per_scenario, opts.mix_period, results, true);
  }
  benchmark.PrintHeader();

  if (opts.enable_pprof) {
    ProfilerStop();
  }

  if (results) {
    return results->WriteJSONFile(opts.perftest_json.value().c_str()) ? 0 : 1;
  }

  return 0;
}

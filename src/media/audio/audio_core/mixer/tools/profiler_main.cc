// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/syslog/cpp/log_settings.h"
#include "src/lib/fxl/command_line.h"
#include "src/media/audio/audio_core/mixer/tools/audio_performance.h"

constexpr char kSpecificBenchmarkSwitch[] = "benchmark";
constexpr char kBenchmarkCreation[] = "creation";
constexpr char kBenchmarkMixing[] = "mixing";
constexpr char kBenchmarkOutput[] = "output";

constexpr char kHelpSwitch[] = "help";
constexpr char kHelp2Switch[] = "?";

void usage(const char* prog_name) {
  printf("\nUsage: %s [--option] [...]\n", prog_name);
  printf("Measure the performance of the audio mixer in microbenchmark operations.\n");
  printf("\nValid options:\n");

  printf("\n    By default, all mixer profilers are executed\n");
  printf("  --%s=<AREA>\t Limit profiling to a single area:\n", kSpecificBenchmarkSwitch);
  printf("\t\t\t     %s\t (mixer creation time)\n", kBenchmarkCreation);
  printf("\t\t\t     %s\t (per-stream mix time)\n", kBenchmarkMixing);
  printf("\t\t\t     %s\t (final output time)\n", kBenchmarkOutput);

  printf("\n  --%s, --%s\t\t Show this message\n\n", kHelpSwitch, kHelp2Switch);
}

int main(int argc, char** argv) {
  syslog::SetTags({"audio_mixer_profiler"});

  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  if (command_line.HasOption(kSpecificBenchmarkSwitch)) {
    std::string specified_benchmark;
    command_line.GetOptionValue(kSpecificBenchmarkSwitch, &specified_benchmark);

    if (specified_benchmark == kBenchmarkCreation) {
      ::media::audio::tools::AudioPerformance::SetBenchmarkCreationOnly();
    } else if (specified_benchmark == kBenchmarkMixing) {
      ::media::audio::tools::AudioPerformance::SetBenchmarkMixingOnly();
    } else if (specified_benchmark == kBenchmarkOutput) {
      ::media::audio::tools::AudioPerformance::SetBenchmarkOutputOnly();
    } else {
      fprintf(stderr, "Unrecognized --%s flag: '%s'\n", kSpecificBenchmarkSwitch,
              specified_benchmark.c_str());
      usage(argv[0]);
      return 1;
    }
  } else if (command_line.HasOption(kHelpSwitch) || command_line.HasOption(kHelp2Switch)) {
    usage(argv[0]);
    return 0;
  } else if (argc > 1) {
    fprintf(stderr, "\nUnrecognized argument\n");
    usage(argv[0]);
    return 1;
  }
  // TODO(fxbug.dev/70393): Additional cmdline flags to specify sampler, format, run count, etc.

  return ::media::audio::tools::AudioPerformance::Profile();
}

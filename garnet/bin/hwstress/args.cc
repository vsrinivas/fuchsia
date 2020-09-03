// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "args.h"

#include <lib/cmdline/args_parser.h>
#include <lib/fitx/result.h>
#include <stdlib.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "src/lib/fxl/strings/string_printf.h"
#include "status.h"

namespace hwstress {
namespace {

std::unique_ptr<cmdline::ArgsParser<CommandLineArgs>> GetParser() {
  auto parser = std::make_unique<cmdline::ArgsParser<CommandLineArgs>>();

  // General flags.
  parser->AddSwitch("duration", 'd', "Test duration in seconds.",
                    &CommandLineArgs::test_duration_seconds);
  parser->AddSwitch("help", 'h', "Show this help.", &CommandLineArgs::help);
  parser->AddSwitch("logging-level", 'l', "Level of logging: terse, normal or verbose.",
                    &CommandLineArgs::log_level);
  parser->AddSwitch("memory", 'm', "Amount of memory to test in megabytes.",
                    &CommandLineArgs::mem_to_test_megabytes);

  // Flash test flags.
  parser->AddSwitch("cleanup-test-partitions", 'c', "Cleanup all existing flash test partitions.",
                    &CommandLineArgs::destroy_partitions);
  parser->AddSwitch("fvm-path", 'f', "Path to Fuchsia Volume Manager.", &CommandLineArgs::fvm_path);
  parser->AddSwitch("iterations", 'i', "Number of times to test the flash.",
                    &CommandLineArgs::iterations);

  // Memory test flags.
  parser->AddSwitch("percent-memory", 0, "Percent of memory to test.",
                    &CommandLineArgs::ram_to_test_percent);

  // CPU test flags.
  parser->AddSwitch("utilization", 'u', "Target CPU utilization percent.",
                    &CommandLineArgs::utilization_percent);
  parser->AddSwitch("workload", 'w',
                    "Name of a CPU workload to use. If not specified, "
                    "all available workloads will be used.",
                    &CommandLineArgs::cpu_workload);

  // Light test flags.
  parser->AddSwitch("light-on-time", 0, "Time in seconds the light should be on for each blink.",
                    &CommandLineArgs::light_on_time_seconds);
  parser->AddSwitch("light-off-time", 0, "Time in seconds the light should be off for each blink.",
                    &CommandLineArgs::light_off_time_seconds);

  return parser;
}

}  // namespace

void PrintUsage() {
  puts(
      R"(usage:
hwstress <subcommand> [options]

Attempts to stress hardware components by placing them under high load.

Subcommands:
  cpu                    Perform a CPU stress test.
  flash                  Perform a flash stress test.
  light                  Perform a device light / LED stress test.
  memory                 Perform a RAM stress test.

Global options:
  -d, --duration=<secs>  Test duration in seconds. A value of "0" (the default)
                         indicates to continue testing until stopped.
  -l, --logging-level    Level of logging to show: terse, normal (the default)
                         or verbose.
  -h, --help             Show this help.

CPU test options:
  -u, --utilization=<percent>
                         Percent of system CPU to use. A value of
                         100 (the default) indicates that all the
                         CPU should be used, while 50 would indicate
                         to use 50% of CPU. Must be strictly greater
                         than 0, and no more than 100.
  -w, --workload=<name>  Run a specific CPU workload. The full list
                         can be determined by using "--workload=list".
                         If not specified, each of the internal
                         workloads will be iterated through repeatedly.

Flash test options:
  -c, --cleanup-test-partitions
                         Cleanup all existing flash test partitions in the
                         system, and then exit without testing. Can be used
                         to clean up persistent test partitions left over from
                         previous flash tests which did not exit cleanly.
  -f, --fvm-path=<path>  Path to Fuchsia Volume Manager.
  -i, --iterations=<number>
                         Number of full write/read cycles to perform before finishing the test.
  -m, --memory=<size>    Amount of flash memory to test, in megabytes.

Light test options:
  --light-on-time=<seconds>
                         Time in seconds each "on" blink should be.
                         Defaults to 0.5.
  --light-off-time=<seconds>
                         Time in seconds each "off" blink should be.
                         Defaults to 0.5.

Memory test options:
  -m, --memory=<size>    Amount of RAM to test, in megabytes.
  --percent-memory=<percent>
                         Percentage of total system RAM to test.
)");
}

fitx::result<std::string, CommandLineArgs> ParseArgs(fbl::Span<const char* const> args) {
  CommandLineArgs result;
  StressTest subcommand;

  // Ensure a subcommand was provided.
  if (args.size() < 2) {
    return fitx::error("A subcommand specifying what type of test to run must be specified.");
  }
  std::string_view first_arg(args.data()[1]);

  // If "--help" or "-h" was provided, don't try parsing anything else.
  if (first_arg == "-h" || first_arg == "--help") {
    result.help = true;
    return fitx::success(result);
  }

  // Parse the subcommand.
  if (first_arg == std::string_view("cpu")) {
    subcommand = StressTest::kCpu;
  } else if (first_arg == std::string_view("flash")) {
    subcommand = StressTest::kFlash;
  } else if (first_arg == std::string_view("memory")) {
    subcommand = StressTest::kMemory;
  } else if (first_arg == std::string_view("light")) {
    subcommand = StressTest::kLight;
  } else {
    return fitx::error(
        fxl::StringPrintf("Unknown subcommand or option: '%s'.", std::string(first_arg).data())
            .c_str());
  }

  fbl::Span other_args = args.subspan(1);  // Strip first element.

  std::unique_ptr<cmdline::ArgsParser<CommandLineArgs>> parser = GetParser();
  std::vector<std::string> params;
  cmdline::Status status = parser->Parse(other_args.size(), other_args.data(), &result, &params);
  if (!status.ok()) {
    return fitx::error(status.error_message().c_str());
  }

  // If help is provided, ignore any further invalid args and just show the
  // help screen.
  if (result.help) {
    return fitx::success(result);
  }

  result.subcommand = subcommand;

  // Validate duration.
  if (result.test_duration_seconds < 0) {
    return fitx::error("Test duration cannot be negative.");
  }

  // Validate logging level.
  if (LogLevelFromString(result.log_level) == LogLevel::kInvalid) {
    return fitx::error("Logging level must be one of: terse, normal or verbose.");
  }

  // Validate memory flags.
  if (result.ram_to_test_percent.has_value()) {
    if (result.ram_to_test_percent.value() <= 0 || result.ram_to_test_percent.value() >= 100) {
      return fitx::error("Percent of RAM to test must be between 1 and 99, inclusive.");
    }
  }
  if (result.mem_to_test_megabytes.has_value()) {
    if (result.mem_to_test_megabytes.value() <= 0) {
      return fitx::error("RAM to test must be strictly positive.");
    }
  }
  if (result.mem_to_test_megabytes.has_value() && result.ram_to_test_percent.has_value()) {
    return fitx::error("--memory and --percent-memory cannot both be specified.");
  }

  // Validate utilization.
  if (result.utilization_percent <= 0.0 || result.utilization_percent > 100.0) {
    return fitx::error("--utilization must be greater than 0%%, and no more than 100%%.");
  }

  // Validate light settings.
  if (result.light_on_time_seconds < 0) {
    return fitx::error("'--light-on-time' cannot be negative.");
  }
  if (result.light_off_time_seconds < 0) {
    return fitx::error("'--light-off-time' cannot be negative.");
  }

  // Validate iterations.
  if (result.iterations) {
    if (result.iterations < 1) {
      return fitx::error("'--iterations' must be at least 1.");
    }
    if (result.test_duration_seconds) {
      return fitx::error("'--duration' and '--iterations' cannot both be specified.");
    }
    if (result.subcommand != StressTest::kFlash) {
      return fitx::error("'--iterations' is only valid for the flash test.");
    }
  }

  // Ensure mandatory flash test argument is provided
  if (result.subcommand == StressTest::kFlash) {
    if (result.destroy_partitions && !result.fvm_path.empty()) {
      return fitx::error(fxl::StringPrintf("Path to Fuchsia Volume Manager invalid with cleanup"));
    }
    if (!result.destroy_partitions && result.fvm_path.empty()) {
      return fitx::error(fxl::StringPrintf("Path to Fuchsia Volume Manager must be specified"));
    }
  }

  // Ensure no more parameters were given.
  if (!params.empty()) {
    return fitx::error(fxl::StringPrintf("Unknown option: '%s'.", params[1].c_str()).c_str());
  }

  return fitx::success(result);
}

}  // namespace hwstress

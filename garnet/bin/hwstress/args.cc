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

namespace hwstress {
namespace {

std::unique_ptr<cmdline::ArgsParser<CommandLineArgs>> GetParser() {
  auto parser = std::make_unique<cmdline::ArgsParser<CommandLineArgs>>();
  parser->AddSwitch("duration", 'd', "Test duration in seconds.",
                    &CommandLineArgs::test_duration_seconds);
  parser->AddSwitch("fvm-path", 'f', "Path to Fuchsia Volume Manager.",
                    &CommandLineArgs::fvm_path);
  parser->AddSwitch("help", 'h', "Show this help.", &CommandLineArgs::help);
  return parser;
}

}  // namespace

void PrintUsage() {
  printf(
      R"(usage:
hwstress <subcommand> [options]

Attempts to stress hardware components by placing them under high load.

Subcommands:
  cpu                    Perform a CPU stress test.
  flash                  Perform a flash stress test.
  memory                 Perform a RAM stress test.

Global options:
  -d, --duration=<secs>  Test duration in seconds. A value of "0" (the default)
                         indicates to continue testing until stopped.
  -h, --help             Show this help.

Flash test options:
  -f  --fvm-path=<path>  Path to Fuchsia Volume Manager
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
  } else {
    return fitx::error(
        fxl::StringPrintf("Unknown subcommand or option: '%s'.", std::string(first_arg).data()).c_str());
  }

  fbl::Span other_args = args.subspan(1); // Strip first element.

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

  // Ensure mandatory flash test argument is provided
  if (result.subcommand == StressTest::kFlash && result.fvm_path.empty()) {
    return fitx::error(fxl::StringPrintf("Path to Fuchsia Volume Manager must be specified"));
  }

  // Ensure no more parameters were given.
  if (params.size() > 0) {
    return fitx::error(fxl::StringPrintf("Unknown option: '%s'.", params[1].c_str()).c_str());
  }

  return fitx::success(result);
}

}  // namespace hwstress

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
#include <vector>

namespace hwstress {
namespace {

std::unique_ptr<cmdline::ArgsParser<CommandLineArgs>> GetParser() {
  auto parser = std::make_unique<cmdline::ArgsParser<CommandLineArgs>>();
  parser->AddSwitch("duration", 'd', "Test duration in seconds.",
                    &CommandLineArgs::test_duration_seconds);
  parser->AddSwitch("help", 'h', "Show this help.", &CommandLineArgs::help);
  return parser;
}

}  // namespace

void PrintUsage() {
  printf(
      R"(usage:
hwstress [options]

Attempts to stress hardware components by placing them under high load.

Options:
  -d, --duration=<secs>  Test duration in seconds. A value of "0" (the default)
                         indicates to continue testing until stopped.
  -h, --help             Show this help.
)");
}

fitx::result<std::string, CommandLineArgs> ParseArgs(fbl::Span<const char* const> args) {
  CommandLineArgs result;

  std::unique_ptr<cmdline::ArgsParser<CommandLineArgs>> parser = GetParser();
  std::vector<std::string> params;
  cmdline::Status status = parser->Parse(args.size(), args.data(), &result, &params);
  if (!status.ok()) {
    return fitx::error(status.error_message().c_str());
  }

  // If help is provided, ignore any further invalid args and just show the
  // help screen.
  if (result.help) {
    return fitx::success(std::move(result));
  }

  // Validate duration.
  if (result.test_duration_seconds < 0) {
    return fitx::error("Test duration cannot be negative.");
  }

  result.params = std::move(params);
  return fitx::success(result);
}

}  // namespace hwstress

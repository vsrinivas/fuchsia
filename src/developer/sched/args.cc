// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "args.h"

#include <lib/cmdline/args_parser.h>
#include <stdlib.h>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace sched {
namespace {

std::unique_ptr<cmdline::ArgsParser<CommandLineArgs>> GetParser() {
  auto parser = std::make_unique<cmdline::ArgsParser<CommandLineArgs>>();
  parser->AddSwitch("priority", 'p', "Run command at the given scheduler priority.",
                    &CommandLineArgs::priority);
  parser->AddSwitch("verbose", 'v', "Add verbose logging.", &CommandLineArgs::verbose);
  parser->AddSwitch("help", 'h', "Show this help.", &CommandLineArgs::help);
  return parser;
}

void PrintUsage() {
  printf(
      R"(usage:
sched [options] (-p <priority>) <cmd> [<args>...]

Apply scheduler parameters to the first thread of the given command.
Further spawned threads will run at the system default priority.

Options:
  -p <priority>       Run command at the given scheduler priority.
                      Valid priorities are 0 to 31, inclusive.

  -v                  Show verbose logging.
  --help              Show this help.
)");
}

}  // namespace

CommandLineArgs ParseArgsOrExit(int argc, const char** argv) {
  CommandLineArgs args;

  std::unique_ptr<cmdline::ArgsParser<CommandLineArgs>> parser = GetParser();
  std::vector<std::string> params;
  cmdline::Status status = parser->Parse(argc, argv, &args, &params);
  if (!status.ok()) {
    fprintf(stderr, "Error: %s\n\n", status.error_message().c_str());
    PrintUsage();
    exit(1);
  }
  if (args.help) {
    PrintUsage();
    exit(0);
  }

  // Ensure a command to run was given.
  if (params.empty() || args.help) {
    fprintf(stderr, "Error: no command to run was given.\n");
    PrintUsage();
    exit(1);
  }

  // Ensure a priority was given.
  if (args.priority < 0) {
    fprintf(stderr, "Error: no scheduling priority given.\n");
    PrintUsage();
    exit(1);
  }

  args.params = std::move(params);
  return args;
}

}  // namespace sched

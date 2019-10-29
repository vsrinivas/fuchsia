// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/shell/console/command_line_options.h"

#include <cmdline/args_parser.h>

namespace shell {

const char kCommandStringHelp[] = R"(  --command-string=<command-string>
  -c <command string>
      Execute the given command string instead of reading commands
      interactively.)";

cmdline::Status ParseCommandLine(int argc, const char** argv, CommandLineOptions* options,
                                 std::vector<std::string>* params) {
  cmdline::ArgsParser<CommandLineOptions> parser;

  parser.AddSwitch("command-string", 'c', kCommandStringHelp, &CommandLineOptions::command_string);

  cmdline::Status status = parser.Parse(argc, argv, options, params);
  if (status.has_error()) {
    return status;
  }

  return cmdline::Status::Ok();
}

}  // namespace shell

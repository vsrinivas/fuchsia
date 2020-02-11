// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/mirror/command_line_options.h"

namespace shell::mirror {

// Appears at the top of the --help output above the switch list.
const char kHelpIntro[] = R"(mirror [ <options> ]

  This tool starts a server that mirrors a local directory remotely (i.e., on
  the target). It's not particularly secure. Use with caution!

Options

)";

const char kPortHelp[] = R"(  --port=<port>
  -p <port>
      Launches the server on the given port.)";

const char kPathHelp[] = R"(  --path=<port>
  -p <path>
      The path (directory) to mirror.)";

const char kHelpHelp[] = R"(  --help
  -h
      Prints all command-line switches.)";

cmdline::Status ParseCommandLine(int argc, const char* argv[], CommandLineOptions* options,
                                 std::vector<std::string>* params) {
  cmdline::ArgsParser<CommandLineOptions> parser;
  bool has_port = false;
  parser.AddSwitch("port", 'p', kPortHelp, &CommandLineOptions::port,
                   [&has_port](const std::string& s) {
                     has_port = true;
                     return cmdline::Status::Ok();
                   });
  parser.AddSwitch("path", 'f', kPathHelp, &CommandLineOptions::path);
  // Special --help switch which doesn't exist in the options structure.
  bool requested_help = false;
  parser.AddGeneralSwitch("help", 'h', kHelpHelp, [&requested_help]() { requested_help = true; });
  cmdline::Status status = parser.Parse(argc, argv, options, params);
  if (!has_port) {
    options->port = 0;
  }
  if (status.has_error()) {
    return status;
  }

  if (requested_help) {
    return cmdline::Status::Error(kHelpIntro + parser.GetHelp());
  }
  return cmdline::Status::Ok();
}

}  // namespace shell::mirror

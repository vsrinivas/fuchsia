// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDLCAT_COMMAND_LINE_OPTIONS_H_
#define TOOLS_FIDLCAT_COMMAND_LINE_OPTIONS_H_

#include <cmdline/args_parser.h>

#include "tools/fidlcat/lib/display_options.h"

namespace fidlcat {

struct CommandLineOptions {
  std::optional<std::string> connect;
  std::optional<std::string> remote_pid;
  std::vector<std::string> filter;
  std::vector<std::string> symbol_paths;
  std::vector<std::string> fidl_ir_paths;
  bool pretty_print = false;
  std::string colors = "always";
  int columns = 0;
};

// Parses the given |argc| and |argv| into the well-defined |options|.  If there
// are strings left over, they are put in |params|.
cmdline::Status ParseCommandLine(int argc, const char* argv[],
                                 CommandLineOptions* options,
                                 DisplayOptions* display_options,
                                 std::vector<std::string>* params);

// Gets the list of .fidl.json files from the command line flags.
//
// For each element in |cli_ir_paths|, add all transitively reachable .fidl.json
// files, and store them in |paths|.  Paths that are not available in the system
// will be added to |bad_paths|.
void ExpandFidlPathsFromOptions(
    std::vector<std::string> cli_ir_paths,
    std::vector<std::unique_ptr<std::istream>>& paths,
    std::vector<std::string>& bad_paths);

}  // namespace fidlcat

#endif  // TOOLS_FIDLCAT_COMMAND_LINE_OPTIONS_H_

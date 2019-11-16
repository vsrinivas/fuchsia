// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_SHELL_CONSOLE_COMMAND_LINE_OPTIONS_H_
#define TOOLS_SHELL_CONSOLE_COMMAND_LINE_OPTIONS_H_

#include <optional>
#include <string>
#include <vector>

#include <cmdline/status.h>

namespace shell {

struct CommandLineOptions {
  std::optional<std::string> command_string;
  std::vector<std::string> fidl_ir_path;
  std::vector<std::string> boot_js_lib_path;
};

// Parses the given command line into options and params.
//
// Returns an error if the command-line is badly formed.
cmdline::Status ParseCommandLine(int argc, const char** argv, CommandLineOptions* options,
                                 std::vector<std::string>* params);
}  // namespace shell

#endif  // TOOLS_SHELL_CONSOLE_COMMAND_LINE_OPTIONS_H_

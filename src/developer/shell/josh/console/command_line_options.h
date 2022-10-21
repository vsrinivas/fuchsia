// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_JOSH_CONSOLE_COMMAND_LINE_OPTIONS_H_
#define SRC_DEVELOPER_SHELL_JOSH_CONSOLE_COMMAND_LINE_OPTIONS_H_

#include <lib/cmdline/status.h>

#include <optional>
#include <string>
#include <vector>

namespace shell {

struct CommandLineOptions {
  std::optional<std::string> command_string;
  std::optional<std::string> run_script_path;
  std::string fidl_ir_path;
  std::string boot_js_lib_path;
  bool line_editor = false;
};

// Parses the given command line into options and params.
//
// Returns an error if the command-line is badly formed.
cmdline::Status ParseCommandLine(int argc, const char** argv, CommandLineOptions* options,
                                 std::vector<std::string>* params);
}  // namespace shell

#endif  // SRC_DEVELOPER_SHELL_JOSH_CONSOLE_COMMAND_LINE_OPTIONS_H_

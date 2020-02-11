// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_SHELL_MIRROR_COMMAND_LINE_OPTIONS_H_
#define SRC_DEVELOPER_SHELL_MIRROR_COMMAND_LINE_OPTIONS_H_

#include <lib/cmdline/args_parser.h>

#include <optional>
#include <string>

namespace shell::mirror {

struct CommandLineOptions {
  uint16_t port;
  std::optional<std::string> path;
};

// Parses the given command line into options and params.
//
// Returns an error if the command-line is badly formed. In addition, --help
// text will be returned as an error.
cmdline::Status ParseCommandLine(int argc, const char* argv[], CommandLineOptions* options,
                                 std::vector<std::string>* params);

}  // namespace shell::mirror

#endif  // SRC_DEVELOPER_SHELL_MIRROR_COMMAND_LINE_OPTIONS_H_

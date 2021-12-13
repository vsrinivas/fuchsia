// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef TOOLS_FIDL_FIDLC_LINTER_COMMAND_LINE_OPTIONS_H_
#define TOOLS_FIDL_FIDLC_LINTER_COMMAND_LINE_OPTIONS_H_

#include <lib/cmdline/status.h>

#include <optional>
#include <string>
#include <vector>

namespace fidl::linter {

struct CommandLineOptions {
  std::vector<std::string> included_checks;
  std::vector<std::string> excluded_checks;
  bool must_find_excluded_checks = false;
  std::string format = "text";
};

// Parses the given command line into options and params.
//
// Returns an error if the command-line is badly formed. In addition, --help
// text will be returned as an error.
cmdline::Status ParseCommandLine(int argc, const char* argv[], CommandLineOptions* options,
                                 std::vector<std::string>* params);

// Returns the fidl-lint usage string.
std::string Usage(const std::string& argv0);

}  // namespace fidl::linter

#endif  // TOOLS_FIDL_FIDLC_LINTER_COMMAND_LINE_OPTIONS_H_

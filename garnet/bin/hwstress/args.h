// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HWSTRESS_ARGS_H_
#define GARNET_BIN_HWSTRESS_ARGS_H_

#include <lib/cmdline/args_parser.h>
#include <lib/fitx/result.h>

#include <optional>
#include <string>
#include <vector>

#include <fbl/span.h>

namespace hwstress {

struct CommandLineArgs {
  // Show help.
  bool help = false;

  // Duration in seconds.
  //
  // A value of "0" indicates forever.
  double test_duration_seconds = 0.0;

  // Remaining command line arguments.
  std::vector<std::string> params;
};

// Print usage information to stdout.
void PrintUsage();

// Parse args, returning failure or the parsed arguments.
fitx::result<std::string, CommandLineArgs> ParseArgs(fbl::Span<const char* const> args);

}  // namespace hwstress

#endif  // GARNET_BIN_HWSTRESS_ARGS_H_

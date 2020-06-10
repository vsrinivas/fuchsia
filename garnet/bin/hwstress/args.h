// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HWSTRESS_ARGS_H_
#define GARNET_BIN_HWSTRESS_ARGS_H_

#include <lib/cmdline/args_parser.h>
#include <lib/fitx/result.h>

#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <fbl/span.h>

namespace hwstress {

// Subcommand to run.
enum class StressTest {
  kCpu,
  kFlash,
  kMemory,
};

struct CommandLineArgs {
  // Show help.
  bool help = false;

  // Path to the Fuchsia Volume Manager
  std::string fvm_path;

  // Duration in seconds.
  //
  // A value of "0" indicates forever.
  double test_duration_seconds = 0.0;

  // The subcommand to run.
  StressTest subcommand;
};

// Print usage information to stdout.
void PrintUsage();

// Parse args, returning failure or the parsed arguments.
fitx::result<std::string, CommandLineArgs> ParseArgs(fbl::Span<const char* const> args);

}  // namespace hwstress

#endif  // GARNET_BIN_HWSTRESS_ARGS_H_

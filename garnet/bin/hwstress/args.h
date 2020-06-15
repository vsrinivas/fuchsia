// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HWSTRESS_ARGS_H_
#define GARNET_BIN_HWSTRESS_ARGS_H_

#include <lib/cmdline/args_parser.h>
#include <lib/fitx/result.h>

#include <istream>
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

// A std::optional<int64_t> that can be used with the arg parsing library.
struct OptionalInt64 : public std::optional<int64_t> {
  OptionalInt64() : std::optional<int64_t>(std::nullopt) {}
  OptionalInt64(int64_t n) : std::optional<int64_t>(n) {}  // NOLINT(google-explicit-constructor)
  OptionalInt64(const OptionalInt64&) = default;
  OptionalInt64& operator=(const OptionalInt64&) = default;
};

// Parse an OptionalUint64.
std::istream& operator>>(std::istream& is, OptionalInt64& result);

// Parsed command line arguments.
struct CommandLineArgs {
  // The subcommand to run.
  StressTest subcommand;

  //
  // Common arguments.
  //

  // Show help.
  bool help = false;

  // Verbose diagnostics.
  bool verbose = false;

  // Duration in seconds.
  //
  // A value of "0" indicates forever.
  double test_duration_seconds = 0.0;

  //
  // Flash-specific arguments.
  //

  // Path to the Fuchsia Volume Manager
  std::string fvm_path;

  //
  // Memory-specific arguments.
  //

  // Amount of RAM to test.
  OptionalInt64 ram_to_test_percent;
  OptionalInt64 ram_to_test_megabytes;
};

// Print usage information to stdout.
void PrintUsage();

// Parse args, returning failure or the parsed arguments.
fitx::result<std::string, CommandLineArgs> ParseArgs(fbl::Span<const char* const> args);

}  // namespace hwstress

#endif  // GARNET_BIN_HWSTRESS_ARGS_H_

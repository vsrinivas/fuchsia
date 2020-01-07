// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/cmdline/args_parser.h>

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/perf_test.h"

namespace {

struct CommandLineOptions {
  std::string out_file;
};

}  // namespace

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);

  // Set up for command line parsing.
  cmdline::ArgsParser<CommandLineOptions> parser;
  parser.AddSwitch("out", 0, "--out\n  [required] JSON file to write perf stats to.",
                   &CommandLineOptions::out_file);

  bool requested_help = false;
  parser.AddGeneralSwitch("help", 0, "--help\n   Print help",
                          [&requested_help]() { requested_help = true; });

  // Parse the command line.
  CommandLineOptions options;
  std::vector<std::string> params;
  if (auto status = parser.Parse(argc, const_cast<const char **>(argv), &options, &params);
      status.has_error()) {
    fprintf(stderr, "Error: %s\n", status.error_message().c_str());
    return 1;
  }
  if (requested_help) {
    // Gtest will have printed its own help in response to --help, and this
    // gets appended.
    fprintf(stderr, "\n\nPerf test options:\n\n%s\n", parser.GetHelp().c_str());
    return 0;
  }

  // Initialize the perf test system.
  if (options.out_file.empty()) {
    fprintf(stderr, "Parameter --out=<output_file> is required.\n");
    return 1;
  }
  if (!zxdb::InitPerfLog(options.out_file)) {
    fprintf(stderr, "Failed to initialize perf log.\n");
    return 1;
  }

  int result = RUN_ALL_TESTS();

  zxdb::FinalizePerfLog();
  return result;
}

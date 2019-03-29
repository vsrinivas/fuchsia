// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This program contains several "tests" that exercise tracing functionality.
// Each test is composed of two pieces: a runner and a verifier.
// Each test is spawned by trace_system_test twice: once to run the runner
// and once to run the verifier. When run as a "runner" this program is
// actually spawned by "trace record". When run as a "verifier", this program
// is invoked directly by trace_system_test.
// See |kUsageString| for usage instructions.
//
// The tests are currently combined into one binary because there aren't
// that many and they share enough code. KISS.

#include <stdlib.h>
#include <iostream>

#include <src/lib/fxl/command_line.h>
#include "src/lib/files/file.h"
#include <src/lib/fxl/log_settings.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <src/lib/fxl/logging.h>

#include "garnet/bin/trace/spec.h"
#include "garnet/bin/trace/tests/integration_test_utils.h"

const char kUsageString[] = {
    "Test runner usage:\n"
    "  $program [options] run tspec-file\n"
    "\n"
    "Test verifier usage:\n"
    "  $program [options] verify tspec-file trace-output-file\n"
    "\n"
    "Options:\n"
    "  --quiet[=LEVEL]    set quietness level (opposite of verbose)\n"
    "  --verbose[=LEVEL]  set debug verbosity level\n"
    "  --log-file=FILE    write log output to FILE\n"};

static int RunTest(const tracing::Spec& spec, TestRunner* run) {
  bool success = run(spec);
  return success ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int VerifyTest(const tracing::Spec& spec, TestVerifier* verify,
                      const std::string& test_output_file) {
  if (!verify(spec, test_output_file))
    return EXIT_FAILURE;
  return EXIT_SUCCESS;
}

static void PrintUsageString() { std::cout << kUsageString << std::endl; }

int main(int argc, char* argv[]) {
  fxl::CommandLine cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl))
    return EXIT_FAILURE;

  // Print this early so that we can see that the program ran.
  // This is very useful when debugging failures in CQ: If there was a
  // problem launching us outside of our control there's nothing in the logs
  // to show we got at least this far.
  FXL_LOG(INFO) << argv[0] << " started";

  if (cl.HasOption("help", nullptr)) {
    PrintUsageString();
    return EXIT_SUCCESS;
  }

  auto args = cl.positional_args();

  if (args.size() < 2) {
    PrintUsageString();
    return EXIT_FAILURE;
  }

  const std::string command = args[0];
  const std::string spec_file_path = args[1];

  if (command == "run") {
    if (args.size() != 2) {
      FXL_LOG(ERROR) << "Wrong number of arguments to run invocation";
      return EXIT_FAILURE;
    }
  } else if (command == "verify") {
    if (args.size() != 3) {
      FXL_LOG(ERROR) << "Wrong number of arguments to verify invocation";
      return EXIT_FAILURE;
    }
  } else {
    FXL_LOG(ERROR) << "Unknown command: " << command;
    return EXIT_FAILURE;
  }

  std::string spec_file_contents;
  if (!files::ReadFileToString(spec_file_path, &spec_file_contents)) {
    FXL_LOG(ERROR) << "Can't read test spec: " << spec_file_path;
    return EXIT_FAILURE;
  }

  tracing::Spec spec;
  if (!tracing::DecodeSpec(spec_file_contents, &spec)) {
    FXL_LOG(ERROR) << "Error decoding test spec: " << spec_file_path;
    return EXIT_FAILURE;
  }

  FXL_DCHECK(spec.test_name);
  auto test_name = *spec.test_name;

  auto test = LookupTest(test_name);
  if (test == nullptr) {
    FXL_LOG(ERROR) << "Unknown test name: " << test_name;
    return EXIT_FAILURE;
  }

  if (command == "run") {
    FXL_LOG(INFO) << "Running subprogram for test " << spec_file_path << ":\""
                  << test_name << "\"";
    return RunTest(spec, test->run);
  } else {
    FXL_LOG(INFO) << "Verifying test " << spec_file_path << ":\"" << test_name
                  << "\"";
    const std::string& trace_output_file = args[2];
    return VerifyTest(spec, test->verify, trace_output_file);
  }
}

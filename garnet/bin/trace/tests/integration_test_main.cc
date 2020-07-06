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

#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <initializer_list>
#include <iostream>

#include <src/lib/files/file.h>

#include "garnet/bin/trace/options.h"
#include "garnet/bin/trace/tests/integration_test_utils.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"
#include "src/lib/fxl/strings/string_number_conversions.h"

const char kUsageString[] = {
    "Test runner usage:\n"
    "  $program [options] run test-name buffer-size buffering-mode\n"
    "\n"
    "Test verifier usage:\n"
    "  $program [options] verify test-name buffer-size buffering-mode trace-output-file\n"
    "\n"
    "Options:\n"
    "  --quiet[=LEVEL]    set quietness level (opposite of verbose)\n"
    "  --verbose[=LEVEL]  set debug verbosity level\n"
    "  --log-file=FILE    write log output to FILE\n"};

static void PrintUsageString() { std::cout << kUsageString << std::endl; }

static bool ParseSize(const std::string& string_value, size_t* int_value) {
  if (!fxl::StringToNumberWithError<size_t>(string_value, int_value)) {
    FX_LOGS(ERROR) << "Failed to parse unsigned integer from string: \"" << string_value << '"';
    return false;
  }
  if (*int_value == 0) {
    FX_LOGS(ERROR) << "String \"" << string_value
                   << "\" parsed to integer 0; expected a positive value";
    return false;
  }
  return true;
}

static bool CopyArguments(const std::vector<std::string>& args,
                          std::initializer_list<std::string*> outputs) {
  if (args.size() != outputs.size() + 1) {
    FX_LOGS(ERROR) << "Wrong number of arguments to " << args[0] << " invocation";
    return false;
  }
  int argument_index = 1;  // skip first arg (command name)
  for (std::string* output : outputs) {
    *output = args[argument_index++];
  }
  return true;
}

int main(int argc, char* argv[]) {
  fxl::CommandLine cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl))
    return EXIT_FAILURE;

  // Print this early so that we can see that the program ran.
  // This is very useful when debugging failures in CQ: If there was a
  // problem launching us outside of our control there's nothing in the logs
  // to show we got at least this far.
  FX_LOGS(INFO) << argv[0] << " started";

  if (cl.HasOption("help", nullptr)) {
    PrintUsageString();
    return EXIT_SUCCESS;
  }

  auto args = cl.positional_args();

  if (args.empty()) {
    PrintUsageString();
    return EXIT_FAILURE;
  }

  const std::string command = args[0];

  if (command == "run") {
    std::string test_name, buffer_size_string, buffering_mode;
    size_t buffer_size;
    if (!(CopyArguments(args, {&test_name, &buffer_size_string, &buffering_mode}) &&
          ParseSize(buffer_size_string, &buffer_size))) {
      return EXIT_FAILURE;  // error already logged
    }

    const tracing::test::IntegrationTest* test = tracing::test::LookupTest(test_name);

    FX_LOGS(INFO) << "Running subprogram for test " << test_name << " with " << buffer_size
                  << " MB " << buffering_mode << " buffer";
    return test->run(buffer_size, buffering_mode) ? EXIT_SUCCESS : EXIT_FAILURE;
  }

  if (command == "verify") {
    std::string test_name, buffer_size_string, buffering_mode, trace_output_file;
    size_t buffer_size;
    if (!(CopyArguments(args,
                        {&test_name, &buffer_size_string, &buffering_mode, &trace_output_file}) &&
          ParseSize(buffer_size_string, &buffer_size))) {
      return EXIT_FAILURE;  // error already logged
    }

    const tracing::test::IntegrationTest* test = tracing::test::LookupTest(test_name);

    FX_LOGS(INFO) << "Verifying test " << test_name << ", output file " << trace_output_file;
    return test->verify(buffer_size, buffering_mode, trace_output_file) ? EXIT_SUCCESS
                                                                        : EXIT_FAILURE;
  }

  FX_LOGS(ERROR) << "Unknown command: " << command;
  return EXIT_FAILURE;
}

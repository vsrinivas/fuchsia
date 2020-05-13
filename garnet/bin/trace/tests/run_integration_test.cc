// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a utility program for running integration tests by hand.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>
#include <stdlib.h>

#include <iostream>

#include "garnet/bin/trace/tests/component_context.h"
#include "garnet/bin/trace/tests/integration_test_utils.h"
#include "garnet/bin/trace/tests/run_test.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/log_settings_command_line.h"

const char kUsageString[] = {
    "Usage: run "
    "fuchsia-pkg://fuchsia.com/trace_tests#meta/run_integration_test.cmx\n"
    "  [options] data/<test>.tspec\n"
    "\n"
    "Note that the tspec path is relative to /pkg.\n"
    "\n"
    "Options:\n"
    "  --quiet[=LEVEL]    set quietness level (opposite of verbose)\n"
    "  --verbose[=LEVEL]  set debug verbosity level\n"
    "  --log-file=FILE    write log output to FILE\n"};

static void PrintUsageString() { std::cout << kUsageString << std::endl; }

int main(int argc, char *argv[]) {
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  syslog::LogSettings log_settings;
  if (!fxl::ParseLogSettings(cl, &log_settings))
    return EXIT_FAILURE;
  syslog::SetLogSettings(log_settings);

  if (cl.HasOption("help", nullptr)) {
    PrintUsageString();
    return EXIT_SUCCESS;
  }

  auto args = cl.positional_args();

  if (args.size() != 1) {
    FX_LOGS(ERROR) << "Missing tspec file";
    return EXIT_FAILURE;
  }
  auto relative_tspec_path = args[0];

  tracing::test::InitComponentContext();

  if (!tracing::test::RunTspec(relative_tspec_path, tracing::test::kRelativeOutputFilePath,
                               log_settings)) {
    return EXIT_FAILURE;
  }

  if (!tracing::test::VerifyTspec(relative_tspec_path, tracing::test::kRelativeOutputFilePath,
                                  log_settings)) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a utility program for running integration tests by hand.

#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <src/lib/fxl/logging.h>
#include <stdlib.h>

#include <iostream>

#include "garnet/bin/trace/tests/run_test.h"

// Note: /data is no longer large enough in qemu sessions
const char kOutputFilePath[] = "/tmp/test.trace";

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
  if (!fxl::SetLogSettingsFromCommandLine(cl))
    return EXIT_FAILURE;

  if (cl.HasOption("help", nullptr)) {
    PrintUsageString();
    return EXIT_SUCCESS;
  }

  auto args = cl.positional_args();

  if (args.size() != 1) {
    FXL_LOG(ERROR) << "Missing tspec file";
    return EXIT_FAILURE;
  }
  auto relative_tspec_path = args[0];

  // |Create()| needs a loop, it uses the default dispatcher.
  std::unique_ptr<sys::ComponentContext> context;
  {
    async::Loop loop(&kAsyncLoopConfigAttachToThread);
    context = sys::ComponentContext::Create();
    FXL_DCHECK(context);
  }

  if (!RunTspec(context.get(), relative_tspec_path, kOutputFilePath)) {
    return EXIT_FAILURE;
  }

  if (!VerifyTspec(context.get(), relative_tspec_path, kOutputFilePath)) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

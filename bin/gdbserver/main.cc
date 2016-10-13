// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "lib/ftl/command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_number_conversions.h"

#include "process.h"
#include "server.h"

namespace {

constexpr char kUsageString[] =
    "Usage: debugserver [options] port program [args...]\n"
    "\n"
    "  port    - TCP port\n"
    "  program - the path to the executable to run\n"
    "\n"
    "e.g. debugserver 2345 /path/to/executable\n"
    "\n"
    "Options:\n"
    "  --help           show this help message\n"
    "  --debug=[level]  set debug verbosity level\n";

constexpr char kDebugOption[] = "debug";

void PrintUsageString() {
  std::cout << kUsageString << std::endl;
}

}  // namespace

int main(int argc, char* argv[]) {
  ftl::CommandLine cl = ftl::CommandLineFromArgcArgv(argc, argv);

  if (cl.HasOption("help", nullptr) || cl.positional_args().size() < 2) {
    PrintUsageString();
    return EXIT_SUCCESS;
  }

  if (cl.HasOption(kDebugOption, nullptr)) {
    std::string debug_level;
    cl.GetOptionValue(kDebugOption, &debug_level);

    // Parse the verbosity level as a positive integer.
    unsigned int level;
    if (!ftl::StringToNumberWithError<unsigned int>(debug_level, &level)) {
      FTL_LOG(ERROR) << "Invalid log level: " << debug_level;
      return EXIT_FAILURE;
    }

    ftl::SetMinLogLevel(-level);
  }

  uint16_t port;
  if (!ftl::StringToNumberWithError<uint16_t>(cl.positional_args()[0], &port)) {
    FTL_LOG(ERROR) << "Not a valid port number: " << cl.positional_args()[0];
    return EXIT_FAILURE;
  }

  FTL_LOG(INFO) << "Starting server.";

  // Create the process. Since we currently support running only one process
  // during a single run of the stub, we initialize it here.
  // TODO(armansito): Change this while adding support for creating and/or
  // attaching to a process later.
  std::vector<std::string> inferior_argv(cl.positional_args().begin() + 1,
                                         cl.positional_args().end());
  auto inferior = std::make_unique<debugserver::Process>(inferior_argv);
  if (!inferior->Initialize()) {
    FTL_LOG(ERROR) << "Failed to set up inferior";
    return EXIT_FAILURE;
  }

  debugserver::Server server(port);
  server.set_current_process(inferior.release());

  bool status = server.Run();
  if (!status) {
    FTL_LOG(ERROR) << "Server exited with error";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

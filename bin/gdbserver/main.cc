// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <iostream>

#include "lib/ftl/command_line.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_number_conversions.h"

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
    "  --help    show this help message\n";

void PrintUsageString() {
  std::cout << kUsageString << std::endl;
}

}  // namespace

int main(int argc, char* argv[]) {
  ftl::CommandLine cl = ftl::CommandLineFromArgcArgv(argc, argv);

  if (cl.HasOption("help", nullptr) || cl.positional_args().size() < 2) {
    PrintUsageString();
    return EXIT_FAILURE;
  }

  uint16_t port;
  if (!ftl::StringToNumberWithError<uint16_t>(cl.positional_args()[0], &port)) {
    FTL_LOG(ERROR) << "Not a valid port number: " << cl.positional_args()[0];
    return EXIT_FAILURE;
  }

  FTL_LOG(INFO) << "Starting server.";

  debugserver::Server server(port);
  bool status = server.Run();
  if (!status) {
    FTL_LOG(ERROR) << "Server exited with error";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

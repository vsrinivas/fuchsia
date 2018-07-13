// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "garnet/lib/inferior_control/process.h"

#include "lib/fsl/handles/object_info.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/log_settings_command_line.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"

#include "server.h"

namespace {

constexpr char kUsageString[] =
    "Usage: debugserver [options] port [program [args...]]\n"
    "       debugserver [options] [--attach=pid] port\n"
    "\n"
    "  port    - TCP port\n"
    "  program - the path to the executable to run\n"
    "  pid     - process id (koid) of the process to attach to\n"
    "\n"
    "Note that only one of program or --attach=pid may be specified.\n"
    "\n"
    "e.g. debugserver 2345 /path/to/executable\n"
    "\n"
    "Options:\n"
    "  --help             show this help message\n"
    "  --verbose[=level]  set debug verbosity level\n"
    "  --quiet[=level]    set quietness level (opposite of verbose)\n"
    "\n"
    "--verbose=<level> : sets |min_log_level| to -level\n"
    "--quiet=<level>   : sets |min_log_level| to +level\n"
    "Quiet supersedes verbose if both are specified.\n"
    "Defined log levels:\n"
    "-n - verbosity level n\n"
    " 0 - INFO - this is the default level\n"
    " 1 - WARNING\n"
    " 2 - ERROR\n"
    " 3 - FATAL\n"
    "Note that negative log levels mean more verbosity.\n";

void PrintUsageString() { std::cout << kUsageString << std::endl; }

}  // namespace

int main(int argc, char* argv[]) {
  fxl::CommandLine cl = fxl::CommandLineFromArgcArgv(argc, argv);

  if (cl.HasOption("help", nullptr)) {
    PrintUsageString();
    return EXIT_SUCCESS;
  }
  if (cl.positional_args().empty()) {
    PrintUsageString();
    return EXIT_FAILURE;
  }

  if (!fxl::SetLogSettingsFromCommandLine(cl))
    return EXIT_FAILURE;

  std::string attach_pid_str;
  zx_koid_t attach_pid = ZX_KOID_INVALID;
  if (cl.GetOptionValue("attach", &attach_pid_str)) {
    if (!fxl::StringToNumberWithError<zx_koid_t>(attach_pid_str, &attach_pid)) {
      FXL_LOG(ERROR) << "Not a valid process id: " << attach_pid_str;
      return EXIT_FAILURE;
    }
  }

  uint16_t port;
  if (!fxl::StringToNumberWithError<uint16_t>(cl.positional_args()[0], &port)) {
    FXL_LOG(ERROR) << "Not a valid port number: " << cl.positional_args()[0];
    return EXIT_FAILURE;
  }

  FXL_LOG(INFO) << "Starting server.";

  // Give this thread an identifiable name for debugging purposes.
  fsl::SetCurrentThreadName("server (main)");

  debugserver::RspServer server(port, attach_pid);

  std::vector<std::string> inferior_argv(cl.positional_args().begin() + 1,
                                         cl.positional_args().end());
  auto inferior = new debugserver::Process(&server, &server);

  // Are we passed a pid or a program?
  if (attach_pid != ZX_KOID_INVALID && !inferior_argv.empty()) {
    FXL_LOG(ERROR) << "Cannot specify both --attach=pid and a program";
    return EXIT_FAILURE;
  }

  // If inferior_argv is empty, it must be supplied by the debugger.
  if (!inferior_argv.empty()) {
    inferior->set_argv(inferior_argv);
  }

  // It's simpler to set the current process here since we don't support
  // multiple processes yet. If running a program, the process is not live yet
  // however, it does not exist to the kernel yet. Calling
  // Process::Initialize() is left to the vRun command.
  server.set_current_process(inferior);

  bool status = server.Run();
  if (!status) {
    FXL_LOG(ERROR) << "Server exited with error";
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}

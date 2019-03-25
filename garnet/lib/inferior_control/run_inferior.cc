// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This is a simple program that serves two purposes:
// 1) Serve as an example of how to use the library.
// 2) Provide a tool to exercise the library by hand.

#include <stdio.h>
#include <stdlib.h>

#include <lib/fxl/command_line.h>
#include <lib/fxl/log_settings_command_line.h>
#include <lib/fxl/logging.h>

#include "garnet/lib/debugger_utils/jobs.h"
#include "garnet/lib/debugger_utils/util.h"

#include "process.h"
#include "server.h"

const char kUsageString[] =
  "Usage: run_inferior [options] [--] path [arg1 ...]\n"
  "Options:\n"
  "  --help    Duh.";

class SampleServer final : public inferior_control::Server {
 public:
  SampleServer()
      : Server(debugger_utils::GetDefaultJob(),
               debugger_utils::GetDefaultJob(),
               sys::ServiceDirectory::CreateFromNamespace()) {
  }

  bool Run() override {
    // Start the main loop.
    __UNUSED zx_status_t status = message_loop_.Run();

    FXL_LOG(INFO) << "Main loop exited";

    return true;
  }

  bool RunInferior(const std::string& path,
                   const debugger_utils::Argv& argv) {
    // The exception port loop must be started before we attach to the
    // inferior.
    if (!exception_port_.Run()) {
      FXL_LOG(ERROR) << "Failed to initialize exception port!";
      return false;
    }

    if (!StartInferior(path, argv)) {
      return false;
    }

    __UNUSED bool success = Run();
    FXL_DCHECK(success);

    // Tell the exception port to quit and wait for it to finish.
    exception_port_.Quit();

    return true;
  }

 private:
  bool StartInferior(const std::string& path,
                     const debugger_utils::Argv& argv) {
    auto inferior = new inferior_control::Process(this, this);
    set_current_process(inferior);

    std::unique_ptr<process::ProcessBuilder> builder;
    if (!CreateProcessViaBuilder(path, argv, &builder)) {
      FXL_LOG(ERROR) << "Unable to create process builder";
      return EXIT_FAILURE;
    }

    builder->CloneAll();

    if (!inferior->InitializeFromBuilder(std::move(builder))) {
      FXL_LOG(ERROR) << "Unable to initialize inferior process";
      return EXIT_FAILURE;
    }

    if (!inferior->Start()) {
      FXL_LOG(ERROR) << "Unable to start process";
      return false;
    }

    return true;
  }
};

static void PrintUsageString() { puts(kUsageString); }

int main(int argc, char **argv) {
  auto cl = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(cl))
    return EXIT_FAILURE;

  if (cl.HasOption("help", nullptr)) {
    PrintUsageString();
    return EXIT_SUCCESS;
  }

  if (cl.positional_args().size() == 0) {
    FXL_LOG(ERROR) << "Missing program";
    return EXIT_FAILURE;
  }
  debugger_utils::Argv inferior_argv{cl.positional_args().begin(),
      cl.positional_args().end()};
  const std::string& path = inferior_argv[0];

  FXL_LOG(INFO) << "Running: " << path;
  FXL_LOG(INFO) << "Args: " << debugger_utils::ArgvToString(inferior_argv);

  SampleServer server{};
  if (!server.RunInferior(path, inferior_argv)) {
    return EXIT_FAILURE;
  }

  inferior_control::Process* inferior = server.current_process();
  if (inferior->return_code_is_set()) {
    FXL_LOG(INFO) << "Process " << inferior->id() << " exited, rc "
                  << inferior->return_code();
    return inferior->return_code();
  } else {
    FXL_LOG(INFO) << "Process " << inferior->id() << " crashed";
    return -1;
  }
}

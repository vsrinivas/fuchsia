// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/component2/cpp/termination_reason.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/log_settings_command_line.h>
#include <lib/fxl/logging.h>
#include <iostream>
#include "sandbox.h"
#include "sandbox_service.h"

using namespace netemul;

void PrintUsage() {
  fprintf(stderr, R"(
Usage: netemul_sandbox [--help] [--component=<package_url>] [-- [arguments...]]

       if *component* is provided, will start a sandbox and run the provided component within it,
       passing *arguments* forward. Return code mimics the exit code of the component under test.

       if *component* is not provided, exposes the fuchsia.netemul.sandbox.Sandbox interface.

       *package_url* takes the form of component manifest URL which uniquely
       identifies a component. Example:
          fuchsia-pkg://fuchsia.com/component_hello_world#meta/hello.cmx

)");
}

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  if (command_line.HasOption("help")) {
    PrintUsage();
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigNoAttachToThread);
  async_set_default_dispatcher(loop.dispatcher());

  SandboxArgs args;
  if (command_line.GetOptionValue("component", &args.package)) {
    args.args.insert(args.args.end(), command_line.positional_args().begin(),
                     command_line.positional_args().end());

    FXL_LOG(INFO) << "Starting netemul sandbox for " << args.package;

    Sandbox sandbox(std::move(args));
    sandbox.SetTerminationCallback([](int64_t exit_code,
                                      Sandbox::TerminationReason reason) {
      FXL_LOG(INFO) << "Sandbox terminated with (" << exit_code << ") reason: "
                    << component2::HumanReadableTerminationReason(reason);
      if (reason != Sandbox::TerminationReason::EXITED) {
        exit_code = 1;
      }
      zx_process_exit(exit_code);
    });

    sandbox.Start(loop.dispatcher());
    loop.Run();
  } else if ((!command_line.options().empty()) ||
             (!command_line.positional_args().empty())) {
    PrintUsage();
    return 1;
  } else {
    FXL_LOG(INFO) << "Exposing fuchsia.netemul.sandbox.Sandbox service";
    SandboxService service(loop.dispatcher());
    auto context = component::StartupContext::CreateFromStartupInfo();
    context->outgoing().AddPublicService(service.GetHandler());
    loop.Run();
  }

  return 0;
}
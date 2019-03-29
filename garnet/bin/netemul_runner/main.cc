// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <lib/async-loop/cpp/loop.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <src/lib/fxl/logging.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/sys/cpp/termination_reason.h>
#include <iostream>
#include "sandbox.h"
#include "sandbox_service.h"

using namespace netemul;

void PrintUsage() {
  fprintf(stderr, R"(
Usage: netemul_sandbox [--help] [--definition=path_to_cmx] [-- [arguments...]]

       if *definition* is provided, will start a sandbox and run the provided environment definition.
       It'll parse the cmx file pointed and create the sandbox following the fuchsia.netemul facet.

       if *definition* is not provided, exposes the fuchsia.netemul.sandbox.Sandbox interface.

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

  std::string definition;
  if (command_line.GetOptionValue("definition", &definition)) {
    SandboxArgs args;

    int root = open("/", O_RDONLY);
    if (!args.ParseFromCmxFileAt(root, definition)) {
      FXL_LOG(ERROR) << "Parsing test definition failed";
      return 1;
    }

    Sandbox sandbox(std::move(args));
    sandbox.SetTerminationCallback([](int64_t exit_code,
                                      Sandbox::TerminationReason reason) {
      FXL_LOG(INFO) << "Sandbox terminated with (" << exit_code << ") reason: "
                    << sys::HumanReadableTerminationReason(reason);
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
    auto context = sys::ComponentContext::Create();
    context->outgoing()->AddPublicService(service.GetHandler());
    loop.Run();
  }

  return 0;
}
// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <trace-provider/provider.h>
#include <memory>

#include "lib/app/cpp/application_context.h"
#include "lib/app_driver/cpp/app_driver.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/macros.h"
#include "peridot/bin/device_runner/cobalt/cobalt.h"
#include "peridot/bin/user_runner/user_runner_impl.h"

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  const bool test = command_line.HasOption("test");

  fsl::MessageLoop loop;
  trace::TraceProvider trace_provider(loop.async());

  std::unique_ptr<app::ApplicationContext> app_context =
      app::ApplicationContext::CreateFromStartupInfo();
  fxl::AutoCall<fxl::Closure> cobalt_cleanup = modular::InitializeCobalt(
      std::move(loop.task_runner()),
      app_context.get());
  modular::AppDriver<modular::UserRunnerImpl> driver(
      app_context->outgoing_services(),
      std::make_unique<modular::UserRunnerImpl>(app_context.get(), test),
      [&loop, &cobalt_cleanup] {
        cobalt_cleanup.call();
        loop.QuitNow();
      });

  loop.Run();
  return 0;
}

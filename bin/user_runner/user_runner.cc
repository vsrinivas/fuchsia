// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <lib/app_driver/cpp/app_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fit/function.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/macros.h>
#include <trace-provider/provider.h>

#include "peridot/bin/device_runner/cobalt/cobalt.h"
#include "peridot/bin/user_runner/user_runner_impl.h"

fxl::AutoCall<fit::closure> SetupCobalt(
    const bool disable_statistics, async_dispatcher_t* dispatcher,
    component::StartupContext* const startup_context) {
  if (disable_statistics) {
    return fxl::MakeAutoCall<fit::closure>([] {});
  }
  return modular::InitializeCobalt(dispatcher, startup_context);
}

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  const bool test = command_line.HasOption("test");

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());
  std::unique_ptr<component::StartupContext> context =
      component::StartupContext::CreateFromStartupInfo();

  fxl::AutoCall<fit::closure> cobalt_cleanup =
      SetupCobalt(test, std::move(loop.dispatcher()), context.get());

  modular::AppDriver<modular::UserRunnerImpl> driver(
      context->outgoing().deprecated_services(),
      std::make_unique<modular::UserRunnerImpl>(context.get(), test),
      [&loop, &cobalt_cleanup] {
        cobalt_cleanup.call();
        loop.Quit();
      });

  loop.Run();
  return 0;
}

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include <memory>

#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/macros.h>
#include <trace-provider/provider.h>

#include "peridot/bin/basemgr/basemgr_impl.h"
#include "peridot/bin/basemgr/basemgr_settings.h"
#include "peridot/bin/basemgr/cobalt/cobalt.h"

fit::deferred_action<fit::closure> SetupCobalt(
    modular::BasemgrSettings& settings, async_dispatcher_t* dispatcher,
    component::StartupContext* context) {
  if (settings.disable_statistics) {
    return fit::defer<fit::closure>([] {});
  }
  return modular::InitializeCobalt(dispatcher, context);
};

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (command_line.HasOption("help")) {
    std::cout << modular::BasemgrSettings::GetUsage() << std::endl;
    return 0;
  }

  modular::BasemgrSettings settings(command_line);
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());
  auto context = std::shared_ptr<component::StartupContext>(
      component::StartupContext::CreateFromStartupInfo());
  fit::deferred_action<fit::closure> cobalt_cleanup =
      SetupCobalt(settings, std::move(loop.dispatcher()), context.get());

  // TODO(MF-98): Assess feasibility of injecting the service dependencies
  // explicitly rather than passing the entire startup context, for easier
  // testing.
  modular::BasemgrImpl basemgr(settings, context, [&loop, &cobalt_cleanup] {
    cobalt_cleanup.call();
    loop.Quit();
  });
  loop.Run();

  return 0;
}

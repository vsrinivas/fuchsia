// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <lib/app_driver/cpp/app_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/macros.h>
#include <lib/fxl/strings/split_string.h>
#include <trace-provider/provider.h>

#include "peridot/bin/basemgr/cobalt/cobalt.h"
#include "peridot/bin/user_runner/user_runner_impl.h"

fit::deferred_action<fit::closure> SetupCobalt(
    const bool disable_statistics, async_dispatcher_t* dispatcher,
    component::StartupContext* const startup_context) {
  if (disable_statistics) {
    return fit::defer<fit::closure>([] {});
  }
  return modular::InitializeCobalt(dispatcher, startup_context);
}

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  modular::UserRunnerImpl::Options opts;
  opts.test = command_line.HasOption("test");
  opts.use_memfs_for_ledger = command_line.HasOption("use_memfs_for_ledger");
  opts.no_cloud_provider_for_ledger =
      command_line.HasOption("no_cloud_provider_for_ledger");

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());
  std::unique_ptr<component::StartupContext> context =
      component::StartupContext::CreateFromStartupInfo();

  auto cobalt_cleanup =
      SetupCobalt(opts.test, std::move(loop.dispatcher()), context.get());

  auto startup_agents = fxl::SplitStringCopy(
      command_line.GetOptionValueWithDefault("startup_agents", ""), ",",
      fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
  auto session_agents = fxl::SplitStringCopy(
      command_line.GetOptionValueWithDefault("session_agents", ""), ",",
      fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);

  for (const auto& startup_agent : startup_agents) {
    opts.startup_agents.push_back(startup_agent);
  }
  for (const auto& session_agent : session_agents) {
    opts.session_agents.push_back(session_agent);
  }

  modular::AppDriver<modular::UserRunnerImpl> driver(
      context->outgoing().deprecated_services(),
      std::make_unique<modular::UserRunnerImpl>(context.get(), opts),
      [&loop, &cobalt_cleanup] {
        cobalt_cleanup.call();
        loop.Quit();
      });

  loop.Run();
  return 0;
}

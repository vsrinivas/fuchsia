// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <fuchsia/modular/internal/cpp/fidl.h>
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
#include "peridot/bin/sessionmgr/sessionmgr_impl.h"

fit::deferred_action<fit::closure> SetupCobalt(
    const bool enable_cobalt, async_dispatcher_t* dispatcher,
    component::StartupContext* const startup_context) {
  if (!enable_cobalt) {
    return fit::defer<fit::closure>([] {});
  }
  return modular::InitializeCobalt(dispatcher, startup_context);
}

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  fuchsia::modular::internal::SessionmgrConfig config;
  config.set_enable_cobalt(command_line.GetOptionValueWithDefault(
                               "enable_statistics", "true") == "true");
  config.set_enable_story_shell_preload(
      command_line.GetOptionValueWithDefault("enable_story_shell_preload",
                                             "true") == "true");
  config.set_use_memfs_for_ledger(
      command_line.HasOption("use_memfs_for_ledger"));

  if (command_line.HasOption("no_cloud_provider_for_ledger")) {
    config.set_cloud_provider(fuchsia::modular::internal::CloudProvider::NONE);
  } else if (command_line.HasOption("use_cloud_provider_from_environment")) {
    config.set_cloud_provider(
        fuchsia::modular::internal::CloudProvider::FROM_ENVIRONMENT);
  } else {
    config.set_cloud_provider(
        fuchsia::modular::internal::CloudProvider::LET_LEDGER_DECIDE);
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());
  std::unique_ptr<component::StartupContext> context =
      component::StartupContext::CreateFromStartupInfo();

  auto cobalt_cleanup = SetupCobalt(
      (config.enable_cobalt()), std::move(loop.dispatcher()), context.get());

  auto startup_agents = fxl::SplitStringCopy(
      command_line.GetOptionValueWithDefault("startup_agents", ""), ",",
      fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
  config.set_startup_agents(std::move(startup_agents));

  auto session_agents = fxl::SplitStringCopy(
      command_line.GetOptionValueWithDefault("session_agents", ""), ",",
      fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
  config.set_session_agents(std::move(session_agents));

  modular::AppDriver<modular::SessionmgrImpl> driver(
      context->outgoing().deprecated_services(),
      std::make_unique<modular::SessionmgrImpl>(context.get(),
                                                std::move(config)),
      [&loop, &cobalt_cleanup] {
        cobalt_cleanup.call();
        loop.Quit();
      });

  loop.Run();
  return 0;
}

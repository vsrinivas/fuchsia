// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/internal/cpp/fidl.h>
#include <lib/app_driver/cpp/app_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/macros.h>
#include <src/lib/fxl/strings/split_string.h>
#include <trace-provider/provider.h>

#include <memory>

#include "peridot/bin/basemgr/cobalt/cobalt.h"
#include "peridot/bin/sessionmgr/sessionmgr_impl.h"
#include "peridot/lib/modular_config/modular_config.h"
#include "peridot/lib/modular_config/modular_config_constants.h"

fit::deferred_action<fit::closure> SetupCobalt(
    const bool enable_cobalt, async_dispatcher_t* dispatcher,
    sys::ComponentContext* component_context) {
  if (!enable_cobalt) {
    return fit::defer<fit::closure>([] {});
  }
  return modular::InitializeCobalt(dispatcher, component_context);
}

void OverrideConfigFromCommandLine(
    fxl::CommandLine command_line,
    fuchsia::modular::session::SessionmgrConfig* config) {
  if (command_line.HasOption(modular_config::kEnableCobalt)) {
    config->set_enable_cobalt(
        command_line.GetOptionValueWithDefault(modular_config::kEnableCobalt,
                                               modular_config::kTrue) ==
        modular_config::kTrue);
  }

  if (command_line.HasOption(modular_config::kEnableStoryShellPreload)) {
    config->set_enable_story_shell_preload(
        command_line.GetOptionValueWithDefault(
            modular_config::kEnableStoryShellPreload, modular_config::kTrue) ==
        modular_config::kTrue);
  }

  if (command_line.HasOption(modular_config::kUseMemfsForLedger)) {
    config->set_use_memfs_for_ledger(true);
  }

  if (command_line.HasOption(modular_config::kNoCloudProviderForLedger)) {
    config->set_cloud_provider(fuchsia::modular::session::CloudProvider::NONE);
  } else if (command_line.HasOption(
                 modular_config::kUseCloudProviderFromEnvironment)) {
    config->set_cloud_provider(
        fuchsia::modular::session::CloudProvider::FROM_ENVIRONMENT);
  }

  if (command_line.HasOption(modular_config::kStartupAgents)) {
    auto startup_agents = fxl::SplitStringCopy(
        command_line.GetOptionValueWithDefault(modular_config::kStartupAgents,
                                               ""),
        ",", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
    config->set_startup_agents(std::move(startup_agents));
  }

  if (command_line.HasOption(modular_config::kSessionAgents)) {
    auto session_agents = fxl::SplitStringCopy(
        command_line.GetOptionValueWithDefault(modular_config::kSessionAgents,
                                               ""),
        ",", fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);
    config->set_session_agents(std::move(session_agents));
  }
}

int main(int argc, const char** argv) {
  // Read configurations from file. This sets default values for any
  // configurations that aren't specified in the configuration.
  auto config_reader = modular::ModularConfigReader::CreateFromNamespace();
  fuchsia::modular::session::SessionmgrConfig config =
      config_reader.GetSessionmgrConfig();

  if (argc > 1) {
    // Read command line arguments if they exist.
    auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);

    // use_default_config is an indication from basemgr that sessionmgr is being
    // launched in a test scenario and that we should use default configs rather
    // than reading product configs from file.
    if (command_line.HasOption("use_default_config")) {
      config = config_reader.GetDefaultSessionmgrConfig();
    }

    OverrideConfigFromCommandLine(command_line, &config);
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  std::unique_ptr<component::StartupContext> context =
      component::StartupContext::CreateFromStartupInfo();
  std::unique_ptr<sys::ComponentContext> component_context(
      sys::ComponentContext::Create());

  auto cobalt_cleanup =
      SetupCobalt((config.enable_cobalt()), std::move(loop.dispatcher()),
                  component_context.get());

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

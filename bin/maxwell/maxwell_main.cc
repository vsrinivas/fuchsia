// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/strings/split_string.h>

#include "peridot/bin/maxwell/config.h"
#include "peridot/bin/maxwell/user_intelligence_provider_impl.h"

namespace maxwell {
namespace {

class App {
 public:
  App(component::StartupContext* context, const Config& config)
      : factory_impl_(context, config) {
    context->outgoing()
        .AddPublicService<fuchsia::modular::UserIntelligenceProviderFactory>(
            [this](fidl::InterfaceRequest<
                   fuchsia::modular::UserIntelligenceProviderFactory>
                       request) {
              factory_bindings_.AddBinding(&factory_impl_, std::move(request));
            });
  }

 private:
  UserIntelligenceProviderFactoryImpl factory_impl_;
  fidl::BindingSet<fuchsia::modular::UserIntelligenceProviderFactory>
      factory_bindings_;
};

}  // namespace
}  // namespace maxwell

const char kUsage[] = R"USAGE(%s
--startup_agents=<agents>
--session_agents=<agents>

  <agents> = comma-separated list of agents
  Example:
    --startup_agents=experiment_agent,usage_log
    --session_agents=kronk,puddy
)USAGE";

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (command_line.HasOption("help")) {
    printf(kUsage, argv[0]);
    return 0;
  }

  maxwell::Config config;
  // Setup startup_agents and session_agents from command line args.
  auto startup_agents = fxl::SplitStringCopy(
      command_line.GetOptionValueWithDefault("startup_agents", ""), ",",
      fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);

  auto session_agents = fxl::SplitStringCopy(
      command_line.GetOptionValueWithDefault("session_agents", ""), ",",
      fxl::kTrimWhitespace, fxl::kSplitWantNonEmpty);

  for (auto agent : startup_agents) {
    config.startup_agents.push_back(agent);
  }
  for (auto agent : session_agents) {
    config.session_agents.push_back(agent);
  }

  FXL_LOG(INFO) << "Starting Maxwell with config: \n" << config;

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = component::StartupContext::CreateFromStartupInfo();
  maxwell::App app(context.get(), config);
  loop.Run();
  return 0;
}

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/hardware/power/statecontrol/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/modular/session/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace-provider/provider.h>

#include "src/lib/files/directory.h"
#include "src/lib/files/path.h"
#include "src/lib/fxl/command_line.h"
#include "src/modular/bin/basemgr/basemgr_impl.h"
#include "src/modular/bin/basemgr/cobalt/cobalt.h"
#include "src/modular/lib/modular_config/modular_config.h"
#include "src/modular/lib/modular_config/modular_config_accessor.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"

// Command-line command to delete the persistent configuration.
constexpr std::string_view kDeletePersistentConfigCommand = "delete_persistent_config";

fit::deferred_action<fit::closure> SetupCobalt(bool enable_cobalt, async_dispatcher_t* dispatcher,
                                               sys::ComponentContext* component_context) {
  if (!enable_cobalt) {
    return fit::defer<fit::closure>([] {});
  }
  return modular::InitializeCobalt(dispatcher, component_context);
};

std::unique_ptr<modular::BasemgrImpl> CreateBasemgrImpl(
    modular::ModularConfigAccessor config_accessor, sys::ComponentContext* component_context,
    async::Loop* loop) {
  fit::deferred_action<fit::closure> cobalt_cleanup = SetupCobalt(
      config_accessor.basemgr_config().enable_cobalt(), loop->dispatcher(), component_context);

  return std::make_unique<modular::BasemgrImpl>(
      std::move(config_accessor), component_context->svc(), component_context->outgoing(),
      component_context->svc()->Connect<fuchsia::sys::Launcher>(),
      component_context->svc()->Connect<fuchsia::ui::policy::Presenter>(),
      component_context->svc()->Connect<fuchsia::hardware::power::statecontrol::Admin>(),
      /*on_shutdown=*/
      [loop, cobalt_cleanup = std::move(cobalt_cleanup), component_context]() mutable {
        cobalt_cleanup.call();
        component_context->outgoing()->debug_dir()->RemoveEntry(modular_config::kBasemgrConfigName);
        loop->Quit();
      });
}

std::string GetUsage() {
  return R"(Usage: basemgr [<command>]

  <command>
    (none)                    Launches basemgr.
    delete_persistent_config  Deletes any existing persistent configuration, and exits.

basemgr cannot be launched from the shell. Please use `basemgr_launcher` or `run`.
)";
}

int main(int argc, const char** argv) {
  syslog::SetTags({"basemgr"});

  auto config_reader = modular::ModularConfigReader::CreateFromNamespace();
  auto config_writer = modular::ModularConfigWriter::CreateFromNamespace();

  // Process command line arguments.
  const auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  const auto& positional_args = command_line.positional_args();
  if (positional_args.size() == 1 && positional_args[0] == kDeletePersistentConfigCommand) {
    if (auto result = config_writer.Delete(); result.is_error()) {
      std::cerr << result.take_error() << std::endl;
      return EXIT_FAILURE;
    }
    std::cout << "Deleted persistent configuration." << std::endl;
    return EXIT_SUCCESS;
  }
  if (!positional_args.empty()) {
    std::cerr << GetUsage() << std::endl;
    return EXIT_FAILURE;
  }

  // Read configuration.
  auto config_result = config_reader.ReadAndMaybePersistConfig(&config_writer);
  if (config_result.is_error()) {
    std::cerr << config_result.take_error() << std::endl;
    return EXIT_FAILURE;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  std::unique_ptr<sys::ComponentContext> component_context(
      sys::ComponentContext::CreateAndServeOutgoingDirectory());

  auto basemgr_impl = CreateBasemgrImpl(modular::ModularConfigAccessor(config_result.take_value()),
                                        component_context.get(), &loop);

  // NOTE: component_controller.events.OnDirectoryReady() is triggered when a
  // component's out directory has mounted. basemgr_launcher uses this signal
  // to determine when basemgr has completed initialization so it can detach
  // and stop itself. When basemgr_launcher is used, it's responsible for
  // providing basemgr a configuration file. To ensure we don't shutdown
  // basemgr_launcher too early, we need additions to out/ to complete after
  // configurations have been parsed.
  component_context->outgoing()->debug_dir()->AddEntry(
      modular_config::kBasemgrConfigName,
      std::make_unique<vfs::Service>([basemgr_impl = basemgr_impl.get()](
                                         zx::channel request, async_dispatcher_t* /* unused */) {
        basemgr_impl->Connect(
            fidl::InterfaceRequest<fuchsia::modular::internal::BasemgrDebug>(std::move(request)));
      }));

  loop.Run();

  // The loop will run until graceful shutdown is complete so returning SUCCESS here indicates that.
  return EXIT_SUCCESS;
}

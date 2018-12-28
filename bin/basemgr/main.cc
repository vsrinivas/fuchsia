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
#include "peridot/lib/session_shell_settings/session_shell_settings.h"

fit::deferred_action<fit::closure> SetupCobalt(
    modular::BasemgrSettings& settings, async_dispatcher_t* dispatcher,
    component::StartupContext* context) {
  if (settings.disable_statistics) {
    return fit::defer<fit::closure>([] {});
  }
  return modular::InitializeCobalt(dispatcher, context);
};

constexpr char kBasemgrDir[] = "basemgr";

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (command_line.HasOption("help")) {
    std::cout << modular::BasemgrSettings::GetUsage() << std::endl;
    return 0;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());
  auto context = std::shared_ptr<component::StartupContext>(
      component::StartupContext::CreateFromStartupInfo());
  if (!context->has_environment_services()) {
    FXL_LOG(ERROR) << "Failed to receive services from the environment.";
    return 1;
  }

  modular::BasemgrSettings settings(command_line);
  auto session_shell_settings =
      modular::SessionShellSettings::GetSystemSettings();

  fit::deferred_action<fit::closure> cobalt_cleanup =
      SetupCobalt(settings, std::move(loop.dispatcher()), context.get());

  fuchsia::ui::policy::PresenterPtr presenter;
  context->ConnectToEnvironmentService(presenter.NewRequest());
  fuchsia::devicesettings::DeviceSettingsManagerPtr device_settings_manager;
  context->ConnectToEnvironmentService(device_settings_manager.NewRequest());

  modular::BasemgrImpl basemgr(
      settings, session_shell_settings, context->launcher().get(),
      std::move(presenter), std::move(device_settings_manager),
      [&loop, &cobalt_cleanup, &context] {
        cobalt_cleanup.call();
        context->outgoing().debug_dir()->RemoveEntry(kBasemgrDir);
        loop.Quit();
      });
  context->outgoing().debug_dir()->AddEntry(
      kBasemgrDir,
      fbl::AdoptRef(new fs::Service([&basemgr](zx::channel channel) {
        fidl::InterfaceRequest<fuchsia::modular::internal::BasemgrDebug>
            request(std::move(channel));
        basemgr.Connect(std::move(request));
        return ZX_OK;
      })));

  loop.Run();

  return 0;
}

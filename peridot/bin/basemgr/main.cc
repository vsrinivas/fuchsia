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
#include "peridot/bin/basemgr/session_shell_settings/session_shell_settings.h"

fit::deferred_action<fit::closure> SetupCobalt(
    bool enable_cobalt, async_dispatcher_t* dispatcher,
    component::StartupContext* context) {
  if (!enable_cobalt) {
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
  modular::BasemgrSettings settings(command_line);
  auto config = settings.CreateBasemgrConfig();
  auto session_shell_settings =
      modular::SessionShellSettings::GetSystemSettings();

  std::vector<fuchsia::modular::internal::SessionShellMapEntry>
      session_shell_configs;
  for (auto setting : session_shell_settings) {
    fuchsia::modular::internal::SessionShellConfig session_shell_config;

    session_shell_config.set_display_usage(setting.display_usage);
    session_shell_config.set_screen_height(setting.screen_height);
    session_shell_config.set_screen_width(setting.screen_width);
    session_shell_config.mutable_app_config()->set_url(setting.name);

    fuchsia::modular::internal::SessionShellMapEntry entry;
    entry.set_name(setting.name);
    entry.set_config(std::move(session_shell_config));

    session_shell_configs.push_back(std::move(entry));
  }

  fit::deferred_action<fit::closure> cobalt_cleanup = SetupCobalt(
      config.enable_cobalt(), std::move(loop.dispatcher()), context.get());

  fuchsia::ui::policy::PresenterPtr presenter;
  context->ConnectToEnvironmentService(presenter.NewRequest());
  fuchsia::devicesettings::DeviceSettingsManagerPtr device_settings_manager;
  context->ConnectToEnvironmentService(device_settings_manager.NewRequest());
  fuchsia::wlan::service::WlanPtr wlan;
  context->ConnectToEnvironmentService(wlan.NewRequest());
  fuchsia::auth::account::AccountManagerPtr account_manager;
  context->ConnectToEnvironmentService(account_manager.NewRequest());

  modular::BasemgrImpl basemgr(
      std::move(config), session_shell_configs, context->launcher().get(),
      std::move(presenter), std::move(device_settings_manager), std::move(wlan),
      std::move(account_manager), [&loop, &cobalt_cleanup, &context] {
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

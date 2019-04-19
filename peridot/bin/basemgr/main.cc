// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/modular/session/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/macros.h>
#include <trace-provider/provider.h>

#include <iostream>
#include <memory>

#include "peridot/bin/basemgr/basemgr_impl.h"
#include "peridot/bin/basemgr/basemgr_settings.h"
#include "peridot/bin/basemgr/cobalt/cobalt.h"
#include "peridot/bin/basemgr/session_shell_settings/session_shell_settings.h"
#include "peridot/lib/modular_config/modular_config.h"
#include "peridot/lib/modular_config/modular_config_constants.h"

namespace {
#ifdef AUTO_LOGIN_TO_GUEST
constexpr bool kAutoLoginToGuest = true;
#else
constexpr bool kAutoLoginToGuest = false;
#endif

}  // namespace

fit::deferred_action<fit::closure> SetupCobalt(
    bool enable_cobalt, async_dispatcher_t* dispatcher,
    component::StartupContext* context) {
  if (!enable_cobalt) {
    return fit::defer<fit::closure>([] {});
  }
  return modular::InitializeCobalt(dispatcher, context);
};

fuchsia::modular::session::BasemgrConfig CreateBasemgrConfigFromCommandLine(
    fxl::CommandLine command_line) {
  modular::BasemgrSettings settings(command_line);
  auto config = settings.CreateBasemgrConfig();

  if (!config.test()) {
    auto session_shell_settings =
        modular::SessionShellSettings::GetSystemSettings();

    // The session shell settings overrides the session_shell flag passed via
    // command line, except in integration tests. We clear the default session
    // shell when |session_shell_settings| is not empty.
    // TODO(MF-340) Remove when all clients are no longer providing
    // base_shell_config.json
    if (session_shell_settings.size() > 0) {
      config.mutable_session_shell_map()->clear();
    }

    for (auto setting : session_shell_settings) {
      fuchsia::modular::session::SessionShellConfig session_shell_config;
      session_shell_config.set_display_usage(setting.display_usage);
      session_shell_config.set_screen_height(setting.screen_height);
      session_shell_config.set_screen_width(setting.screen_width);
      session_shell_config.mutable_app_config()->set_url(setting.name);
      session_shell_config.mutable_app_config()->set_args({});

      fuchsia::modular::session::SessionShellMapEntry entry;
      entry.set_name(setting.name);
      entry.set_config(std::move(session_shell_config));

      config.mutable_session_shell_map()->push_back(std::move(entry));
    }
  }

  return config;
}

int main(int argc, const char** argv) {
  fuchsia::modular::session::BasemgrConfig config;

  if (argc == 1) {
    // Read configurations from file if no command line arguments are passed in.
    // This sets default values for any configurations that aren't specified in
    // the configuration file.
    modular::ModularConfigReader config_reader;
    config = config_reader.GetBasemgrConfig();

    config.mutable_sessionmgr()->set_url(modular_config::kSessionmgrUrl);
    config.mutable_sessionmgr()->set_args({});
  } else {
    // Read command line arguments
    auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
    if (command_line.HasOption("help")) {
      std::cout << modular::BasemgrSettings::GetUsage() << std::endl;
      return 0;
    }

    config = CreateBasemgrConfigFromCommandLine(command_line);
  }

  // When auto-login to guest is specified, we use dev_base_shell with user
  // specified such that a persistent guest user is created on first-time boot.
  if (kAutoLoginToGuest) {
    fuchsia::modular::session::AppConfig override_base_shell;
    override_base_shell.set_url(modular_config::kDefaultBaseShellUrl);
    override_base_shell.mutable_args()->push_back("--user=persistent_guest");
    config.mutable_base_shell()->set_app_config(std::move(override_base_shell));
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());
  auto context = std::shared_ptr<component::StartupContext>(
      component::StartupContext::CreateFromStartupInfo());

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
      std::move(config), context->launcher().get(), std::move(presenter),
      std::move(device_settings_manager), std::move(wlan),
      std::move(account_manager), [&loop, &cobalt_cleanup, &context] {
        cobalt_cleanup.call();
        context->outgoing().debug_dir()->RemoveEntry(
            modular_config::kBasemgrConfigName);
        loop.Quit();
      });
  context->outgoing().debug_dir()->AddEntry(
      modular_config::kBasemgrConfigName,
      fbl::AdoptRef(new fs::Service([&basemgr](zx::channel channel) {
        fidl::InterfaceRequest<fuchsia::modular::internal::BasemgrDebug>
            request(std::move(channel));
        basemgr.Connect(std::move(request));
        return ZX_OK;
      })));

  loop.Run();

  return 0;
}

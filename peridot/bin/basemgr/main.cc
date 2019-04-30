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
  return settings.CreateBasemgrConfig();
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

  // When auto-login to guest is specified, we use auto_login_base_shell with
  // user specified such that a persistent guest user is created on first-time
  // boot.
  if (kAutoLoginToGuest) {
    fuchsia::modular::session::AppConfig override_base_shell;
    override_base_shell.set_url(modular_config::kDefaultBaseShellUrl);
    override_base_shell.mutable_args()->push_back("--persist_user");
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

// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/modular/session/cpp/fidl.h>
#include <fuchsia/setui/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
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

// Base shell URL's used to implement login override. See c.f.
// ConfigureLoginOverride.
constexpr char kAutoLoginBaseShellUrl[] =
    "fuchsia-pkg://fuchsia.com/auto_login_base_shell#meta/"
    "auto_login_base_shell.cmx";

constexpr char kSingleUserBaseShellUrl[] =
    "fuchsia-pkg://fuchsia.com/single_user_base_shell#meta/"
    "single_user_base_shell.cmx";
}  // namespace

fit::deferred_action<fit::closure> SetupCobalt(
    bool enable_cobalt, async_dispatcher_t* dispatcher,
    sys::ComponentContext* component_context) {
  if (!enable_cobalt) {
    return fit::defer<fit::closure>([] {});
  }
  return modular::InitializeCobalt(dispatcher, component_context);
};

fuchsia::modular::session::BasemgrConfig CreateBasemgrConfigFromCommandLine(
    fxl::CommandLine command_line) {
  modular::BasemgrSettings settings(command_line);
  return settings.CreateBasemgrConfig();
}

bool LoginModeOverrideIsSpecified(
    const fuchsia::setui::SettingsObject& settings_obj,
    fuchsia::setui::LoginOverride login_override) {
  return settings_obj.data.is_account() &&
         settings_obj.data.account().has_mode() &&
         settings_obj.data.account().mode() == login_override;
}

// Configures base shell based on user specified login override.
//
// 1. Reads the login override settings stored in SetUiService.
// 2. Overrides the base shell to the base shell corresponding to the login
// override mode.
// 3. If SetUiService did not have any login override settings initially,
// write the login mode corresponding to the base shell being configured.
void ConfigureLoginOverride(fuchsia::modular::session::BasemgrConfig& config,
                            const fuchsia::setui::SettingsObject& settings_obj,
                            fuchsia::setui::SetUiService* setui) {
  // Get the login override settings. There is no scenario where both will be
  // true.
  bool auth_provider_mode_requested = LoginModeOverrideIsSpecified(
      settings_obj, fuchsia::setui::LoginOverride::AUTH_PROVIDER);
  bool guest_mode_requested = LoginModeOverrideIsSpecified(
      settings_obj, fuchsia::setui::LoginOverride::AUTOLOGIN_GUEST);

  // Any value of LoginOverride will override the build flag
  // |auto_login_to_guest|.
  if (kAutoLoginToGuest) {
    fuchsia::modular::session::AppConfig override_base_shell;
    override_base_shell.set_url(kAutoLoginBaseShellUrl);
    override_base_shell.mutable_args()->push_back("--persist_user");
    config.mutable_base_shell()->set_app_config(std::move(override_base_shell));
  }

  if (auth_provider_mode_requested) {
    FXL_LOG(INFO) << "Login Override: auth provider mode";
    // When auth provider mode is specified, we use single_user_base_shell.
    // The framework expects this package to be available in all product
    // configurations.
    fuchsia::modular::session::AppConfig override_base_shell;
    override_base_shell.set_url(kSingleUserBaseShellUrl);
    config.mutable_base_shell()->set_app_config(std::move(override_base_shell));
  } else if (guest_mode_requested) {
    FXL_LOG(INFO) << "Login Override: Guest mode";
    // When guest mode is specified, we use auto_login_base_shell with
    // a persistent guest user. The framework expects this package to be
    // available in all product configurations.
    fuchsia::modular::session::AppConfig override_base_shell;
    override_base_shell.set_url(kAutoLoginBaseShellUrl);
    override_base_shell.mutable_args()->push_back("--persist_user");
    config.mutable_base_shell()->set_app_config(std::move(override_base_shell));
  } else {
    // In the case no login mode is specified, propagate the default login mode
    // to setui service so that other components are aware of this state.
    if (settings_obj.data.is_account() &&
        settings_obj.data.account().has_mode() &&
        settings_obj.data.account().mode() !=
            fuchsia::setui::LoginOverride::NONE) {
      return;
    }

    // Determine the default login mode by looking at which base shell is being
    // used for this product configuration.
    fuchsia::setui::AccountMutation account_mutation;
    account_mutation.set_operation(
        fuchsia::setui::AccountOperation::SET_LOGIN_OVERRIDE);
    if (config.base_shell().app_config().url() == kAutoLoginBaseShellUrl) {
      account_mutation.set_login_override(
          fuchsia::setui::LoginOverride::AUTOLOGIN_GUEST);
    } else if (config.base_shell().app_config().url() ==
               kSingleUserBaseShellUrl) {
      account_mutation.set_login_override(
          fuchsia::setui::LoginOverride::AUTH_PROVIDER);
    } else {
      return;
    }

    fuchsia::setui::Mutation mutation;
    mutation.set_account_mutation_value(std::move(account_mutation));

    // There is no need to wait for this callback as the result does not
    // affect basemgr.
    setui->Mutate(
        fuchsia::setui::SettingType::ACCOUNT, std::move(mutation),
        [](fuchsia::setui::MutationResponse response) {
          if (response.return_code != fuchsia::setui::ReturnCode::OK) {
            FXL_LOG(ERROR) << "Failed to persist login type";
          }
        });
  }
}

// Configures Basemgr by passing in connected services.
std::unique_ptr<modular::BasemgrImpl> ConfigureBasemgr(
    fuchsia::modular::session::BasemgrConfig& config,
    std::shared_ptr<component::StartupContext> context,
    sys::ComponentContext* component_context, async::Loop* loop) {
  fit::deferred_action<fit::closure> cobalt_cleanup = SetupCobalt(
      config.enable_cobalt(), loop->dispatcher(), component_context);

  fuchsia::ui::policy::PresenterPtr presenter;
  context->ConnectToEnvironmentService(presenter.NewRequest());
  fuchsia::devicesettings::DeviceSettingsManagerPtr device_settings_manager;
  context->ConnectToEnvironmentService(device_settings_manager.NewRequest());
  fuchsia::wlan::service::WlanPtr wlan;
  context->ConnectToEnvironmentService(wlan.NewRequest());
  fuchsia::auth::account::AccountManagerPtr account_manager;
  context->ConnectToEnvironmentService(account_manager.NewRequest());

  return std::make_unique<modular::BasemgrImpl>(
      std::move(config), context->launcher().get(), std::move(presenter),
      std::move(device_settings_manager), std::move(wlan),
      std::move(account_manager), [&loop, &cobalt_cleanup, &context] {
        cobalt_cleanup.call();
        context->outgoing().debug_dir()->RemoveEntry(
            modular_config::kBasemgrConfigName);
        loop->Quit();
      });
}

int main(int argc, const char** argv) {
  fuchsia::modular::session::BasemgrConfig config;

  if (argc == 1) {
    // Read configurations from file if no command line arguments are passed in.
    // This sets default values for any configurations that aren't specified in
    // the configuration file.
    auto config_reader = modular::ModularConfigReader::CreateFromNamespace();
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

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());
  auto context = std::shared_ptr<component::StartupContext>(
      component::StartupContext::CreateFromStartupInfo());
  std::unique_ptr<sys::ComponentContext> component_context(
      sys::ComponentContext::Create());

  fuchsia::setui::SetUiServicePtr setui;
  std::unique_ptr<modular::BasemgrImpl> basemgr_impl;
  auto initialize_basemgr =
      [&config, &loop, &context, &component_context, &basemgr_impl, &setui,
       ran = false](fuchsia::setui::SettingsObject settings_obj) mutable {
        if (ran) {
          return;
        }

        ran = true;

        ConfigureLoginOverride(config, settings_obj, setui.get());

        std::unique_ptr<modular::BasemgrImpl> basemgr =
            ConfigureBasemgr(config, context, component_context.get(), &loop);

        basemgr_impl = std::move(basemgr);

        context->outgoing().debug_dir()->AddEntry(
            modular_config::kBasemgrConfigName,
            fbl::AdoptRef(new fs::Service([basemgr_impl = basemgr_impl.get()](
                                              zx::channel channel) {
              fidl::InterfaceRequest<fuchsia::modular::internal::BasemgrDebug>
                  request(std::move(channel));
              basemgr_impl->Connect(std::move(request));
              return ZX_OK;
            })));
      };

  setui.set_error_handler([&initialize_basemgr](zx_status_t status) {
    // In case of error, log event and continue as if no user setting is
    // present.
    FXL_LOG(ERROR) << "Error retrieving user set login override, defaulting to "
                      "build time configuration";
    fuchsia::setui::AccountSettings default_account_settings;
    default_account_settings.set_mode(fuchsia::setui::LoginOverride::NONE);
    fuchsia::setui::SettingsObject default_settings_object;
    default_settings_object.setting_type = fuchsia::setui::SettingType::ACCOUNT;
    default_settings_object.data.set_account(
        std::move(default_account_settings));

    initialize_basemgr(std::move(default_settings_object));
  });

  context->ConnectToEnvironmentService(setui.NewRequest());

  // Retrieve the user specified login override. This override will take
  // precedence over any compile time configuration. Watch will always
  // return a value in this case, as it is the initial request (long
  // polling).
  setui->Watch(fuchsia::setui::SettingType::ACCOUNT, initialize_basemgr);

  loop.Run();

  return 0;
}

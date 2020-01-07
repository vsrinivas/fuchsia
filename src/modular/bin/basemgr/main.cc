// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/device/manager/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/modular/session/cpp/fidl.h>
#include <fuchsia/setui/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/defer.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/macros.h>
#include <trace-provider/provider.h>

#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/macros.h"
#include "src/modular/bin/basemgr/basemgr_impl.h"
#include "src/modular/bin/basemgr/cobalt/cobalt.h"
#include "src/modular/lib/modular_config/modular_config.h"
#include "src/modular/lib/modular_config/modular_config_constants.h"

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
}  // namespace

fit::deferred_action<fit::closure> SetupCobalt(bool enable_cobalt, async_dispatcher_t* dispatcher,
                                               sys::ComponentContext* component_context) {
  if (!enable_cobalt) {
    return fit::defer<fit::closure>([] {});
  }
  return modular::InitializeCobalt(dispatcher, component_context);
};

bool LoginModeOverrideIsSpecified(const fuchsia::setui::SettingsObject& settings_obj,
                                  fuchsia::setui::LoginOverride login_override) {
  return settings_obj.data.is_account() && settings_obj.data.account().has_mode() &&
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
  // Get the login override settings.
  bool guest_mode_requested =
      LoginModeOverrideIsSpecified(settings_obj, fuchsia::setui::LoginOverride::AUTOLOGIN_GUEST);

  // Any value of LoginOverride will override the build flag
  // |auto_login_to_guest|.
  if (kAutoLoginToGuest) {
    fuchsia::modular::session::AppConfig override_base_shell;
    override_base_shell.set_url(kAutoLoginBaseShellUrl);
    override_base_shell.mutable_args()->push_back("--persist_user");
    config.mutable_base_shell()->set_app_config(std::move(override_base_shell));
  }

  if (guest_mode_requested) {
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
    if (settings_obj.data.is_account() && settings_obj.data.account().has_mode() &&
        settings_obj.data.account().mode() != fuchsia::setui::LoginOverride::NONE) {
      return;
    }

    // Determine the default login mode by looking at which base shell is being
    // used for this product configuration.
    fuchsia::setui::AccountMutation account_mutation;
    account_mutation.set_operation(fuchsia::setui::AccountOperation::SET_LOGIN_OVERRIDE);
    if (config.base_shell().app_config().url() == kAutoLoginBaseShellUrl) {
      account_mutation.set_login_override(fuchsia::setui::LoginOverride::AUTOLOGIN_GUEST);
    } else {
      return;
    }

    fuchsia::setui::Mutation mutation;
    mutation.set_account_mutation_value(std::move(account_mutation));

    // There is no need to wait for this callback as the result does not
    // affect basemgr.
    setui->Mutate(fuchsia::setui::SettingType::ACCOUNT, std::move(mutation),
                  [](fuchsia::setui::MutationResponse response) {
                    if (response.return_code != fuchsia::setui::ReturnCode::OK) {
                      FXL_LOG(ERROR) << "Failed to persist login type";
                    }
                  });
  }
}

// Configures Basemgr by passing in connected services.
std::unique_ptr<modular::BasemgrImpl> ConfigureBasemgr(
    fuchsia::modular::session::ModularConfig& config, sys::ComponentContext* component_context,
    async::Loop* loop) {
  fit::deferred_action<fit::closure> cobalt_cleanup =
      SetupCobalt(config.basemgr_config().enable_cobalt(), loop->dispatcher(), component_context);

  fuchsia::ui::policy::PresenterPtr presenter;
  component_context->svc()->Connect(presenter.NewRequest());
  fuchsia::devicesettings::DeviceSettingsManagerPtr device_settings_manager;
  component_context->svc()->Connect(device_settings_manager.NewRequest());
  fuchsia::wlan::service::WlanPtr wlan;
  component_context->svc()->Connect(wlan.NewRequest());
  fuchsia::identity::account::AccountManagerPtr account_manager;
  component_context->svc()->Connect(account_manager.NewRequest());
  fuchsia::device::manager::AdministratorPtr administrator;
  component_context->svc()->Connect(administrator.NewRequest());

  return std::make_unique<modular::BasemgrImpl>(
      std::move(config), component_context->svc(), component_context->outgoing(),
      component_context->svc()->Connect<fuchsia::sys::Launcher>(), std::move(presenter),
      std::move(device_settings_manager), std::move(wlan), std::move(account_manager),
      std::move(administrator),
      [loop, cobalt_cleanup = std::move(cobalt_cleanup), component_context]() mutable {
        cobalt_cleanup.call();
        component_context->outgoing()->debug_dir()->RemoveEntry(modular_config::kBasemgrConfigName);
        loop->Quit();
      });
}

// Delegates lifecycle requests to BasemgrImpl if available. Otherwise, exits the given async loop.
class LifecycleImpl : public fuchsia::modular::Lifecycle {
 public:
  LifecycleImpl(async::Loop* loop, sys::ComponentContext* component_context) : loop_(loop) {
    component_context->outgoing()->AddPublicService<fuchsia::modular::Lifecycle>(
        bindings_.GetHandler(this));
  }

  void set_sink(modular::BasemgrImpl* basemgr) { basemgr_ = basemgr; }

 private:
  // |fuchsia::modular::Lifecycle|
  void Terminate() override {
    if (basemgr_) {
      basemgr_->Terminate();
    } else {
      loop_->Quit();
    }
  }

  modular::BasemgrImpl* basemgr_ = nullptr;
  async::Loop* loop_;
  fidl::BindingSet<fuchsia::modular::Lifecycle> bindings_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  fuchsia::modular::session::ModularConfig modular_config;

  if (argc == 1) {
    // Read configurations from file if no command line arguments are passed in.
    // This sets default values for any configurations that aren't specified in
    // the configuration file.
    auto config_reader = modular::ModularConfigReader::CreateFromNamespace();
    modular_config.set_basemgr_config(config_reader.GetBasemgrConfig());
    modular_config.set_sessionmgr_config(config_reader.GetSessionmgrConfig());

    modular_config.mutable_basemgr_config()->mutable_sessionmgr()->set_url(
        modular_config::kSessionmgrUrl);
    modular_config.mutable_basemgr_config()->mutable_sessionmgr()->set_args({});
  } else {
    std::cerr << "basemgr does not support arguments. Please use basemgr_launcher to "
              << "launch basemgr with custom configurations." << std::endl;
    return 1;
  }

  trace::TraceProviderWithFdio trace_provider(loop.dispatcher());
  std::unique_ptr<sys::ComponentContext> component_context(sys::ComponentContext::Create());

  fuchsia::setui::SetUiServicePtr setui;
  std::unique_ptr<modular::BasemgrImpl> basemgr_impl;
  LifecycleImpl lifecycle_impl(&loop, component_context.get());
  bool ran = false;
  auto initialize_basemgr = [&modular_config, &loop, &component_context, &basemgr_impl,
                             &lifecycle_impl, &setui,
                             &ran](fuchsia::setui::SettingsObject settings_obj) mutable {
    if (ran) {
      return;
    }

    ran = true;

    ConfigureLoginOverride(*modular_config.mutable_basemgr_config(), settings_obj, setui.get());

    std::unique_ptr<modular::BasemgrImpl> basemgr =
        ConfigureBasemgr(modular_config, component_context.get(), &loop);

    basemgr_impl = std::move(basemgr);
    lifecycle_impl.set_sink(basemgr_impl.get());

    // NOTE: component_controller.events.OnDirectoryReady() is triggered when a
    // component's out directory has mounted. basemgr_launcher uses this signal
    // to determine when basemgr has completed initialization so it can detach
    // and stop itself. When basemgr_launcher is used, it's responsible for
    // providing basemgr a configuration file. To ensure we don't shutdown
    // basemgr_launcher too early, we need additions to out/ to complete after
    // configurations have been parsed.
    component_context->outgoing()->debug_dir()->AddEntry(
        modular_config::kBasemgrConfigName,
        std::make_unique<vfs::Service>([basemgr_impl = basemgr_impl.get()](zx::channel request,
                                                                           async_dispatcher_t*) {
          basemgr_impl->Connect(
              fidl::InterfaceRequest<fuchsia::modular::internal::BasemgrDebug>(std::move(request)));
        }));
  };

  setui.set_error_handler([&initialize_basemgr](zx_status_t) {
    // In case of error, log event and continue as if no user setting is
    // present.
    FXL_LOG(ERROR) << "Error retrieving user set login override, defaulting to "
                      "build time configuration";
    fuchsia::setui::AccountSettings default_account_settings;
    default_account_settings.set_mode(fuchsia::setui::LoginOverride::NONE);
    fuchsia::setui::SettingsObject default_settings_object;
    default_settings_object.setting_type = fuchsia::setui::SettingType::ACCOUNT;
    default_settings_object.data.set_account(std::move(default_account_settings));

    initialize_basemgr(std::move(default_settings_object));
  });

  component_context->svc()->Connect(setui.NewRequest());

  // Retrieve the user specified login override. This override will take
  // precedence over any compile time configuration. Watch will always
  // return a value in this case, as it is the initial request (long
  // polling).
  setui->Watch(fuchsia::setui::SettingType::ACCOUNT, initialize_basemgr);

  loop.Run();

  return 0;
}

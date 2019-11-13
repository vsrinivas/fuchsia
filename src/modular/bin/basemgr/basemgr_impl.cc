// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/basemgr_impl.h"

#include <fuchsia/device/manager/cpp/fidl.h>
#include <fuchsia/ui/app/cpp/fidl.h>
#include <lib/fidl/cpp/type_converter.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

#include "src/lib/fsl/types/type_converters.h"
#include "src/lib/fxl/logging.h"
#include "src/modular/bin/basemgr/basemgr_settings.h"
#include "src/modular/bin/basemgr/intl_property_provider_impl/intl_property_provider_impl.h"
#include "src/modular/bin/basemgr/noop_clipboard_impl.h"
#include "src/modular/bin/basemgr/session_provider.h"
#include "src/modular/bin/basemgr/wait_for_minfs.h"
#include "src/modular/lib/common/async_holder.h"
#include "src/modular/lib/common/teardown.h"
#include "src/modular/lib/fidl/app_client.h"
#include "src/modular/lib/fidl/clone.h"

namespace fidl {
template <>
// fidl::TypeConverter specialization for fuchsia::modular::session::AppConfig
// TODO(MF-277) Convert all usages of fuchsia::modular::AppConfig to
// fuchsia::modular::session::AppConfig and remove this converter.
struct TypeConverter<fuchsia::modular::AppConfig, fuchsia::modular::session::AppConfig> {
  // Converts fuchsia::modular::session::AppConfig to
  // fuchsia::modular::AppConfig
  static fuchsia::modular::AppConfig Convert(const fuchsia::modular::session::AppConfig& config) {
    fuchsia::modular::AppConfig app_config;
    app_config.url = config.url().c_str();

    if (config.has_args()) {
      app_config.args = fidl::To<fidl::VectorPtr<std::string>>(config.args());
    }

    return app_config;
  }
};
}  // namespace fidl

namespace modular {

using cobalt_registry::ModularLifetimeEventsMetricDimensionEventType;

namespace {

// TODO(MF-134): This key is duplicated in
// topaz/lib/settings/lib/device_info.dart. Remove this key once factory reset
// is provided to topaz as a service.
// The key for factory reset toggles.
constexpr char kFactoryResetKey[] = "FactoryReset";

constexpr char kTokenManagerFactoryUrl[] =
    "fuchsia-pkg://fuchsia.com/token_manager_factory#meta/"
    "token_manager_factory.cmx";

}  // namespace

BasemgrImpl::BasemgrImpl(fuchsia::modular::session::ModularConfig config,
                         const std::shared_ptr<sys::ServiceDirectory> incoming_services,
                         const std::shared_ptr<sys::OutgoingDirectory> outgoing_services,
                         fuchsia::sys::LauncherPtr launcher,
                         fuchsia::ui::policy::PresenterPtr presenter,
                         fuchsia::devicesettings::DeviceSettingsManagerPtr device_settings_manager,
                         fuchsia::wlan::service::WlanPtr wlan,
                         fuchsia::identity::account::AccountManagerPtr account_manager,
                         fuchsia::device::manager::AdministratorPtr device_administrator,
                         fit::function<void()> on_shutdown)
    : config_(std::move(config)),
      component_context_services_(std::move(incoming_services)),
      outgoing_services_(std::move(outgoing_services)),
      launcher_(std::move(launcher)),
      presenter_(std::move(presenter)),
      device_settings_manager_(std::move(device_settings_manager)),
      wlan_(std::move(wlan)),
      account_manager_(std::move(account_manager)),
      device_administrator_(std::move(device_administrator)),
      on_shutdown_(std::move(on_shutdown)),
      base_shell_context_binding_(this),
      authentication_context_provider_binding_(this),
      session_provider_("SessionProvider") {
  UpdateSessionShellConfig();

  Start();
}

BasemgrImpl::~BasemgrImpl() = default;

void BasemgrImpl::Connect(
    fidl::InterfaceRequest<fuchsia::modular::internal::BasemgrDebug> request) {
  basemgr_bindings_.AddBinding(this, std::move(request));
}

void BasemgrImpl::StartBaseShell() {
  if (base_shell_running_) {
    FXL_DLOG(INFO) << "StartBaseShell() called when already running";

    return;
  }

  auto base_shell_config =
      fidl::To<fuchsia::modular::AppConfig>(config_.basemgr_config().base_shell().app_config());

  fuchsia::sys::ServiceProviderPtr service_provider;
  services_.AddBinding(service_provider.NewRequest());
  noop_clipboard_ = std::make_unique<NoopClipboardImpl>();

  services_.AddService<fuchsia::modular::Clipboard>(
      [this](fidl::InterfaceRequest<fuchsia::modular::Clipboard> request) {
        noop_clipboard_->Connect(std::move(request));
      });
  fuchsia::sys::ServiceListPtr service_list(new fuchsia::sys::ServiceList);
  service_list->names.push_back(fuchsia::modular::Clipboard::Name_);
  service_list->provider = std::move(service_provider);

  base_shell_app_ = std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
      launcher_.get(), std::move(base_shell_config), /* data_origin = */ "",
      std::move(service_list));
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  fuchsia::ui::app::ViewProviderPtr base_shell_view_provider;
  base_shell_app_->services().ConnectToService(base_shell_view_provider.NewRequest());
  base_shell_view_provider->CreateView(std::move(view_token.value), nullptr, nullptr);

  presentation_container_ = std::make_unique<PresentationContainer>(
      presenter_.get(), std::move(view_holder_token),
      /* shell_config= */ GetActiveSessionShellConfig(),
      /* on_swap_session_shell= */ [this] { SelectNextSessionShell(/* callback= */ [] {}); });

  // TODO(alexmin): Remove BaseShellParams.
  fuchsia::modular::BaseShellParams params;
  base_shell_app_->services().ConnectToService(base_shell_.NewRequest());
  base_shell_->Initialize(base_shell_context_binding_.NewBinding(),
                          /* base_shell_params= */ std::move(params));

  base_shell_running_ = true;
}

FuturePtr<> BasemgrImpl::StopBaseShell() {
  if (!base_shell_running_) {
    FXL_DLOG(INFO) << "StopBaseShell() called when already stopped";

    return Future<>::CreateCompleted("StopBaseShell::Completed");
  }

  auto did_stop = Future<>::Create("StopBaseShell");

  base_shell_app_->Teardown(kBasicTimeout, [did_stop, this] {
    FXL_DLOG(INFO) << "- fuchsia::modular::BaseShell down";

    base_shell_running_ = false;
    did_stop->Complete();
  });

  return did_stop;
}

FuturePtr<> BasemgrImpl::StopTokenManagerFactoryApp() {
  if (token_manager_factory_app_) {
    // Force kill;  token manager does not implement |fuchsia::modular::Lifecycle|.
    token_manager_factory_app_.reset();
  } else {
    FXL_DLOG(INFO) << "StopTokenManagerFactoryApp(): TokenManagerFactory already not running.";
  }
  return Future<>::CreateCompleted("StopTokenManagerFactoryApp completed");
}

void BasemgrImpl::Start() {
  if (config_.basemgr_config().test()) {
    // Print test banner.
    FXL_LOG(INFO) << std::endl
                  << std::endl
                  << "======================== Starting Test ["
                  << config_.basemgr_config().test_name() << "]" << std::endl
                  << "============================================================" << std::endl;
  }

  // Wait for persistent data to come up.
  if (config_.basemgr_config().use_minfs()) {
    WaitForMinfs();
  }

  auto sessionmgr_config =
      fidl::To<fuchsia::modular::AppConfig>(config_.basemgr_config().sessionmgr());
  auto story_shell_config =
      fidl::To<fuchsia::modular::AppConfig>(config_.basemgr_config().story_shell().app_config());
  auto intl_property_provider = IntlPropertyProviderImpl::Create(component_context_services_);
  outgoing_services_->AddPublicService(intl_property_provider->GetHandler());
  session_provider_.reset(new SessionProvider(
      /* delegate= */ this, launcher_.get(), std::move(device_administrator_),
      std::move(sessionmgr_config), CloneStruct(session_shell_config_),
      std::move(story_shell_config),
      config_.basemgr_config().use_session_shell_for_story_shell_factory(),
      std::move(intl_property_provider), CloneStruct(config_),
      /* on_zero_sessions= */
      [this] {
        if (state_ == State::SHUTTING_DOWN) {
          return;
        }
        if (config_.basemgr_config().base_shell().keep_alive_after_login()) {
          // TODO(MI4-1117): Integration tests currently
          // expect base shell to always be running. So, if
          // we're running under a test, DidLogin() will not
          // shut down the base shell after login; thus this
          // method doesn't need to re-start the base shell
          // after a logout.
          return;
        }

        FXL_DLOG(INFO) << "Re-starting due to logout";
        ShowSetupOrLogin();
      }));

  InitializeUserProvider();

  ReportEvent(ModularLifetimeEventsMetricDimensionEventType::BootedToBaseMgr);
}

void BasemgrImpl::InitializeUserProvider() {
  token_manager_factory_app_.release();
  fuchsia::modular::AppConfig token_manager_config;
  token_manager_config.url = kTokenManagerFactoryUrl;
  token_manager_factory_app_ = std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
      launcher_.get(), CloneStruct(token_manager_config));
  token_manager_factory_app_->services().ConnectToService(token_manager_factory_.NewRequest());

  session_user_provider_impl_ = std::make_unique<SessionUserProviderImpl>(
      account_manager_.get(), token_manager_factory_.get(),
      authentication_context_provider_binding_.NewBinding().Bind(),
      /* on_initialize= */
      [this] { ShowSetupOrLogin(); },
      /* on_login= */
      [this](fuchsia::modular::auth::AccountPtr account,
             fuchsia::auth::TokenManagerPtr ledger_token_manager,
             fuchsia::auth::TokenManagerPtr agent_token_manager) {
        OnLogin(std::move(account), std::move(ledger_token_manager),
                std::move(agent_token_manager));
      });
}

void BasemgrImpl::GetUserProvider(fidl::InterfaceRequest<fuchsia::modular::UserProvider> request) {
  session_user_provider_impl_->Connect(std::move(request));
}

void BasemgrImpl::Shutdown() {
  FXL_LOG(INFO) << "BASEMGR SHUTDOWN";
  // Prevent the shutdown sequence from running twice.
  if (state_ == State::SHUTTING_DOWN) {
    return;
  }

  state_ = State::SHUTTING_DOWN;

  if (config_.basemgr_config().test()) {
    FXL_LOG(INFO) << std::endl
                  << "============================================================" << std::endl
                  << "======================== [" << config_.basemgr_config().test_name()
                  << "] Done";
  }

  // |session_provider_| teardown is asynchronous because it holds the
  // sessionmgr processes.
  session_provider_.Teardown(kSessionProviderTimeout, [this] {
    StopBaseShell()->Then([this] {
      FXL_DLOG(INFO) << "- fuchsia::modular::BaseShell down";
      session_user_provider_impl_.reset();
      FXL_DLOG(INFO) << "- fuchsia::modular::UserProvider down";
      StopTokenManagerFactoryApp()->Then([this] {
        FXL_DLOG(INFO) << "- fuchsia::auth::TokenManagerFactory down";
        on_shutdown_();
      });
    });
  });
}

void BasemgrImpl::Terminate() { Shutdown(); }

void BasemgrImpl::GetAuthenticationUIContext(
    fidl::InterfaceRequest<fuchsia::auth::AuthenticationUIContext> request) {
  // TODO(MI4-1107): Basemgr needs to implement AuthenticationUIContext
  // itself, and proxy calls for StartOverlay & StopOverlay to BaseShell,
  // starting it if it's not running yet.
  base_shell_->GetAuthenticationUIContext(std::move(request));
}

void BasemgrImpl::OnLogin(fuchsia::modular::auth::AccountPtr account,
                          fuchsia::auth::TokenManagerPtr ledger_token_manager,
                          fuchsia::auth::TokenManagerPtr agent_token_manager) {
  auto [view_token, view_holder_token] = scenic::ViewTokenPair::New();

  auto did_start_session = session_provider_->StartSession(
      std::move(view_token), std::move(account), std::move(ledger_token_manager),
      std::move(agent_token_manager));
  if (!did_start_session) {
    FXL_LOG(WARNING) << "Session was already started and the logged in user "
                        "could not join the session.";
    return;
  }

  // TODO(MI4-1117): Integration tests currently expect base shell to always be
  // running. So, if we're running under a test, do not shut down the base shell
  // after login.
  if (!config_.basemgr_config().base_shell().keep_alive_after_login()) {
    FXL_DLOG(INFO) << "Stopping base shell due to login";
    StopBaseShell();
  }

  // Ownership of the Presenter should be moved to the session shell for tests
  // that enable presenter, and production code.
  if (!config_.basemgr_config().test() || config_.basemgr_config().enable_presenter()) {
    presentation_container_ = std::make_unique<PresentationContainer>(
        presenter_.get(), std::move(view_holder_token),
        /* shell_config= */ GetActiveSessionShellConfig(),
        /* on_swap_session_shell= */ [this] { SelectNextSessionShell(/* callback= */ [] {}); });
  }
}

void BasemgrImpl::SelectNextSessionShell(SelectNextSessionShellCallback callback) {
  if (state_ == State::SHUTTING_DOWN) {
    FXL_DLOG(INFO) << "SelectNextSessionShell() not supported while shutting down";
    callback();
    return;
  }

  if (config_.basemgr_config().session_shell_map().empty()) {
    FXL_DLOG(INFO) << "No session shells has been defined";
    callback();
    return;
  }
  auto shell_count = config_.basemgr_config().session_shell_map().size();
  if (shell_count <= 1) {
    FXL_DLOG(INFO) << "Only one session shell has been defined so switch is disabled";
    callback();
    return;
  }

  active_session_shell_configs_index_ = (active_session_shell_configs_index_ + 1) % shell_count;

  UpdateSessionShellConfig();

  session_provider_->SwapSessionShell(CloneStruct(session_shell_config_))
      ->Then([callback = std::move(callback)] {
        FXL_LOG(INFO) << "Swapped session shell";
        callback();
      });
}

fuchsia::modular::session::SessionShellConfig BasemgrImpl::GetActiveSessionShellConfig() {
  return CloneStruct(config_.basemgr_config()
                         .session_shell_map()
                         .at(active_session_shell_configs_index_)
                         .config());
}

void BasemgrImpl::UpdateSessionShellConfig() {
  session_shell_config_ =
      CloneStruct(fidl::To<fuchsia::modular::AppConfig>(config_.basemgr_config()
                                                            .session_shell_map()
                                                            .at(active_session_shell_configs_index_)
                                                            .config()
                                                            .app_config()));
}

void BasemgrImpl::ShowSetupOrLogin() {
  auto show_setup_or_login = [this] {
    // If there are no session shell settings specified, default to showing
    // setup.
    if (!config_.basemgr_config().test() &&
        active_session_shell_configs_index_ >=
            config_.basemgr_config().session_shell_map().size()) {
      StartBaseShell();
      return;
    }

    // Login as the first user, or show setup. This asssumes that:
    // 1) Basemgr has exclusive access to AccountManager.
    // 2) There are only 0 or 1 authenticated accounts ever.
    account_manager_->GetAccountIds([this](std::vector<uint64_t> account_ids) {
      if (account_ids.empty()) {
        StartBaseShell();
      } else {
        fuchsia::modular::UserLoginParams2 params;
        params.account_id = std::to_string(account_ids.at(0));
        session_user_provider_impl_->Login2(std::move(params));
      }
    });
  };

  // TODO(MF-347): Handle scenario where device settings manager channel is
  // dropped before error handler is set.
  // TODO(MF-134): Modular should not be handling factory reset.
  // If the device needs factory reset, remove all the users before proceeding
  // with setup.
  device_settings_manager_.set_error_handler(
      [show_setup_or_login](zx_status_t status) { show_setup_or_login(); });
  device_settings_manager_->GetInteger(
      kFactoryResetKey,
      [this, show_setup_or_login](int factory_reset_value, fuchsia::devicesettings::Status status) {
        if (status == fuchsia::devicesettings::Status::ok && factory_reset_value > 0) {
          FXL_LOG(INFO) << "Factory reset initiated";
          // Unset the factory reset flag.
          device_settings_manager_->SetInteger(kFactoryResetKey, 0, [](bool result) {
            if (!result) {
              FXL_LOG(WARNING) << "Factory reset flag was not updated.";
            }
          });

          session_user_provider_impl_->RemoveAllUsers(
              [this] { wlan_->ClearSavedNetworks([this] { StartBaseShell(); }); });
        } else {
          show_setup_or_login();
        }
      });
}

void BasemgrImpl::RestartSession(RestartSessionCallback on_restart_complete) {
  session_provider_->RestartSession(std::move(on_restart_complete));
}

void BasemgrImpl::LoginAsGuest() {
  fuchsia::modular::UserLoginParams2 params;
  session_user_provider_impl_->Login2(std::move(params));
}

void BasemgrImpl::LogoutUsers(fit::function<void()> callback) {
  session_user_provider_impl_->RemoveAllUsers(std::move(callback));
}

void BasemgrImpl::GetPresentation(
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) {
  presentation_container_->GetPresentation(std::move(request));
}

}  // namespace modular

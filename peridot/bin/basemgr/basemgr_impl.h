// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_BASEMGR_BASEMGR_IMPL_H_
#define PERIDOT_BIN_BASEMGR_BASEMGR_IMPL_H_

#include <memory>

#include <fuchsia/auth/cpp/fidl.h>
#include <fuchsia/devicesettings/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/wlan/service/cpp/fidl.h>
#include <lib/async/cpp/future.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/string.h>
#include <src/lib/fxl/macros.h>

#include "peridot/bin/basemgr/basemgr_settings.h"
#include "peridot/bin/basemgr/cobalt/cobalt.h"
#include "peridot/bin/basemgr/presentation_container.h"
#include "peridot/bin/basemgr/session_provider.h"
#include "peridot/bin/basemgr/user_provider_impl.h"
#include "peridot/lib/fidl/clone.h"

namespace modular {

// Basemgr is the parent process of the modular framework, and it is started by
// the sysmgr as part of the boot sequence.
//
// It has several high-level responsibilites:
// 1) Initializes and owns the system's root view and presentation.
// 2) Sets up the interactive flow for user authentication and login.
// 3) Manages the lifecycle of sessions, represented as |sessionmgr| processes.
class BasemgrImpl : fuchsia::modular::BaseShellContext,
                    fuchsia::auth::AuthenticationContextProvider,
                    fuchsia::modular::internal::BasemgrDebug,
                    modular::SessionProvider::Delegate {
 public:
  // Initializes as BasemgrImpl instance with the given parameters:
  //
  // |config| Configs that are parsed from command line. These will be read from
  // a configuration file with the completion of MF-10. Used to configure
  // the modular framework environment.
  // |session_shell_configs| Settings relevant to session shells. Used to
  // configure session shells that are launched.
  // |launcher| Environment service for creating component instances.
  // |presenter| Service to initialize the presentation.
  // |device_settings_manager| Service to look-up whether device needs factory
  // reset.
  // |on_shutdown| Callback invoked when this basemgr instance is shutdown.
  explicit BasemgrImpl(
      fuchsia::modular::internal::BasemgrConfig config,
      const std::vector<fuchsia::modular::internal::SessionShellMapEntry>&
          session_shell_configs,
      fuchsia::sys::Launcher* const launcher,
      fuchsia::ui::policy::PresenterPtr presenter,
      fuchsia::devicesettings::DeviceSettingsManagerPtr device_settings_manager,
      fuchsia::wlan::service::WlanPtr wlan,
      fuchsia::auth::account::AccountManagerPtr account_manager,
      fit::function<void()> on_shutdown);

  ~BasemgrImpl() override;

  void Connect(
      fidl::InterfaceRequest<fuchsia::modular::internal::BasemgrDebug> request);

 private:
  void StartBaseShell();

  FuturePtr<> StopBaseShell();

  FuturePtr<> StopTokenManagerFactoryApp();

  void Start();

  // |fuchsia::modular::BaseShellContext|
  void GetUserProvider(
      fidl::InterfaceRequest<fuchsia::modular::UserProvider> request) override;

  // |fuchsia::modular::BaseShellContext|
  void Shutdown() override;

  // |fuchsia::auth::AuthenticationContextProvider|
  void GetAuthenticationUIContext(
      fidl::InterfaceRequest<fuchsia::auth::AuthenticationUIContext> request)
      override;

  void SwapSessionShell();

  void ShowSetupOrLogin();

  // Invoked when a user has been logged in. Starts a new session for the given
  // |account|.
  void OnLogin(fuchsia::modular::auth::AccountPtr account,
               fuchsia::auth::TokenManagerPtr ledger_token_manager,
               fuchsia::auth::TokenManagerPtr agent_token_manager);

  // Returns the session shell config of the active session shell, or returns
  // the a default config if there is no active one.
  fuchsia::modular::internal::SessionShellConfig GetActiveSessionShellConfig();

  // Updates the session shell app config to the active session shell. Done once
  // on initialization and every time the session shells are swapped.
  void UpdateSessionShellConfig();

  // |BasemgrDebug|
  void RestartSession(RestartSessionCallback on_restart_complete) override;

  // |BasemgrDebug|
  void LoginAsGuest() override;

  // |SessionProvider::Delegate|
  void LogoutUsers(fit::function<void()> callback) override;

  // |SessionProvider::Delegate| and |fuchsia::modular::BaseShellContext|
  void GetPresentation(fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>
                           request) override;

  fuchsia::modular::internal::BasemgrConfig config_;

  // Used to configure which session shell component to launch.
  fuchsia::modular::AppConfig session_shell_config_;

  // |session_shell_configs_| contains the session shell's presentation
  // settings. |active_session_shell_configs_index_| indicates which settings
  // in |session_shell_configs_| is currently active. If
  // |session_shell_configs_| is empty, a default session shell config
  // is used instead.
  const std::vector<fuchsia::modular::internal::SessionShellMapEntry>&
      session_shell_configs_;
  std::vector<fuchsia::modular::internal::SessionShellConfig>::size_type
      active_session_shell_configs_index_{};

  // Used to launch component instances, such as the base shell.
  fuchsia::sys::Launcher* const launcher_;  // Not owned.
  // Used to connect the |presentation_container_| to scenic.
  fuchsia::ui::policy::PresenterPtr presenter_;
  // Used to look-up whether device needs a factory reset.
  fuchsia::devicesettings::DeviceSettingsManagerPtr device_settings_manager_;
  // Used to reset Wi-Fi during factory reset.
  fuchsia::wlan::service::WlanPtr wlan_;
  // Used for account management in the framework.
  fuchsia::auth::account::AccountManagerPtr account_manager_;
  fit::function<void()> on_shutdown_;

  // Holds the presentation service.
  std::unique_ptr<PresentationContainer> presentation_container_;

  std::unique_ptr<UserProviderImpl> user_provider_impl_;

  fidl::BindingSet<fuchsia::modular::internal::BasemgrDebug> basemgr_bindings_;
  fidl::Binding<fuchsia::modular::BaseShellContext> base_shell_context_binding_;
  fidl::Binding<fuchsia::auth::AuthenticationContextProvider>
      authentication_context_provider_binding_;

  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>>
      token_manager_factory_app_;
  fuchsia::auth::TokenManagerFactoryPtr token_manager_factory_;

  bool base_shell_running_{};
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> base_shell_app_;
  fuchsia::modular::BaseShellPtr base_shell_;

  AsyncHolder<SessionProvider> session_provider_;

  enum class State {
    // normal mode of operation
    RUNNING,
    // basemgr is shutting down.
    SHUTTING_DOWN
  };

  State state_ = State::RUNNING;

  FXL_DISALLOW_COPY_AND_ASSIGN(BasemgrImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_BASEMGR_BASEMGR_IMPL_H_

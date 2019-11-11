// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_BASEMGR_BASEMGR_IMPL_H_
#define SRC_MODULAR_BIN_BASEMGR_BASEMGR_IMPL_H_

#include <fuchsia/auth/cpp/fidl.h>
#include <fuchsia/device/manager/cpp/fidl.h>
#include <fuchsia/devicesettings/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/modular/session/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/wlan/service/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fit/function.h>
#include <lib/svc/cpp/service_namespace.h>
#include <lib/sys/cpp/component_context.h>

#include <memory>

#include "src/lib/fxl/macros.h"
#include "src/modular/bin/basemgr/basemgr_settings.h"
#include "src/modular/bin/basemgr/cobalt/cobalt.h"
#include "src/modular/bin/basemgr/noop_clipboard_impl.h"
#include "src/modular/bin/basemgr/presentation_container.h"
#include "src/modular/bin/basemgr/session_provider.h"
#include "src/modular/bin/basemgr/session_user_provider_impl.h"
#include "src/modular/lib/async/cpp/future.h"
#include "src/modular/lib/fidl/clone.h"

namespace modular {

// Basemgr is the parent process of the modular framework, and it is started by
// the sysmgr as part of the boot sequence.
//
// It has several high-level responsibilities:
// 1) Initializes and owns the system's root view and presentation.
// 2) Sets up the interactive flow for user authentication and login.
// 3) Manages the lifecycle of sessions, represented as |sessionmgr| processes.
class BasemgrImpl : public fuchsia::modular::Lifecycle,
                    fuchsia::modular::BaseShellContext,
                    fuchsia::auth::AuthenticationContextProvider,
                    fuchsia::modular::internal::BasemgrDebug,
                    modular::SessionProvider::Delegate {
 public:
  // Initializes as BasemgrImpl instance with the given parameters:
  //
  // |config| Configs that are parsed from command line. These will be read from
  // a configuration file with the completion of MF-10. Used to configure
  // the modular framework environment.
  // |launcher| Environment service for creating component instances.
  // |presenter| Service to initialize the presentation.
  // |device_settings_manager| Service to look-up whether device needs factory
  // reset.
  // |on_shutdown| Callback invoked when this basemgr instance is shutdown.
  explicit BasemgrImpl(fuchsia::modular::session::ModularConfig config,
                       const std::shared_ptr<sys::ServiceDirectory> incoming_services,
                       const std::shared_ptr<sys::OutgoingDirectory> outgoing_services,
                       fuchsia::sys::LauncherPtr launcher,
                       fuchsia::ui::policy::PresenterPtr presenter,
                       fuchsia::devicesettings::DeviceSettingsManagerPtr device_settings_manager,
                       fuchsia::wlan::service::WlanPtr wlan,
                       fuchsia::identity::account::AccountManagerPtr account_manager,
                       fuchsia::device::manager::AdministratorPtr device_administrator,
                       fit::function<void()> on_shutdown);

  ~BasemgrImpl() override;

  void Connect(fidl::InterfaceRequest<fuchsia::modular::internal::BasemgrDebug> request);

  // |fuchsia::modular::Lifecycle|
  void Terminate() override;

 private:
  void StartBaseShell();

  FuturePtr<> StopBaseShell();

  FuturePtr<> StopTokenManagerFactoryApp();

  // Starts the basemgr functionalities in the following order:
  // 1. Initialize session provider.
  // 2. Initialize user provider.
  // 3. Show setup or launch a session.
  void Start();

  // Initializes either the |session_user_provider_impl_| or the
  // |user_provider_impl_| depending on |kUseAccountManager| flag in
  // basemgr_impl.cc. These classes provide modular framework the ability to
  // add/remove/list users and control their participation in sessions.
  void InitializeUserProvider();

  // |fuchsia::modular::BaseShellContext|
  void GetUserProvider(fidl::InterfaceRequest<fuchsia::modular::UserProvider> request) override;

  void Shutdown() override;

  // |fuchsia::auth::AuthenticationContextProvider|
  void GetAuthenticationUIContext(
      fidl::InterfaceRequest<fuchsia::auth::AuthenticationUIContext> request) override;

  // |fuchsia::modular::internal::BasemgrDebug|
  // Toggles to the next session shell in basemgr.config if one exists.
  // |callback| resolves once session shell has been swapped.
  void SelectNextSessionShell(SelectNextSessionShellCallback callback) override;

  void ShowSetupOrLogin();

  // Invoked when a user has been logged in. Starts a new session for the given
  // |account|.
  void OnLogin(fuchsia::modular::auth::AccountPtr account,
               fuchsia::auth::TokenManagerPtr ledger_token_manager,
               fuchsia::auth::TokenManagerPtr agent_token_manager);

  // Returns the session shell config of the active session shell, or returns
  // the a default config if there is no active one.
  fuchsia::modular::session::SessionShellConfig GetActiveSessionShellConfig();

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
  void GetPresentation(fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) override;

  fuchsia::modular::session::ModularConfig config_;

  // Used to configure which session shell component to launch.
  fuchsia::modular::AppConfig session_shell_config_;

  // |active_session_shell_configs_index_| indicates which settings
  // in |config_.session_shell_map()| is currently active.
  std::vector<fuchsia::modular::session::SessionShellConfig>::size_type
      active_session_shell_configs_index_{};

  // Retained to be used in creating a `SessionProvider`.
  const std::shared_ptr<sys::ServiceDirectory> component_context_services_;

  // Used to export fuchsia.intl.PropertyProvider
  const std::shared_ptr<sys::OutgoingDirectory> outgoing_services_;

  // Used to launch component instances, such as the base shell.
  fuchsia::sys::LauncherPtr launcher_;
  // Used to connect the |presentation_container_| to scenic.
  fuchsia::ui::policy::PresenterPtr presenter_;
  // Used to look-up whether device needs a factory reset.
  fuchsia::devicesettings::DeviceSettingsManagerPtr device_settings_manager_;
  // Used to reset Wi-Fi during factory reset.
  fuchsia::wlan::service::WlanPtr wlan_;
  // Used for account management in the framework.
  fuchsia::identity::account::AccountManagerPtr account_manager_;
  // Used to trigger device reboot.
  fuchsia::device::manager::AdministratorPtr device_administrator_;
  fit::function<void()> on_shutdown_;

  // Holds the presentation service.
  std::unique_ptr<PresentationContainer> presentation_container_;

  // Manages the session-user mappings.
  std::unique_ptr<SessionUserProviderImpl> session_user_provider_impl_;

  fidl::BindingSet<fuchsia::modular::internal::BasemgrDebug> basemgr_bindings_;
  fidl::Binding<fuchsia::modular::BaseShellContext> base_shell_context_binding_;
  fidl::Binding<fuchsia::auth::AuthenticationContextProvider>
      authentication_context_provider_binding_;

  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> token_manager_factory_app_;
  fuchsia::auth::TokenManagerFactoryPtr token_manager_factory_;

  bool base_shell_running_{};
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> base_shell_app_;
  fuchsia::modular::BaseShellPtr base_shell_;

  AsyncHolder<SessionProvider> session_provider_;

  std::unique_ptr<NoopClipboardImpl> noop_clipboard_;
  component::ServiceNamespace services_;

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

#endif  // SRC_MODULAR_BIN_BASEMGR_BASEMGR_IMPL_H_

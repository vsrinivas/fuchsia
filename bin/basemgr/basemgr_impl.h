// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_BASEMGR_BASEMGR_IMPL_H_
#define PERIDOT_BIN_BASEMGR_BASEMGR_IMPL_H_

#include <memory>

#include <fuchsia/auth/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/viewsv1/cpp/fidl.h>
#include <fuchsia/ui/viewsv1token/cpp/fidl.h>
#include <lib/async/cpp/future.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fidl/cpp/interface_handle.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/fidl/cpp/string.h>
#include <lib/fxl/macros.h>

#include "peridot/bin/basemgr/basemgr_settings.h"
#include "peridot/bin/basemgr/cobalt/cobalt.h"
#include "peridot/bin/basemgr/user_provider_impl.h"
#include "peridot/lib/session_shell_settings/session_shell_settings.h"

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
                    fuchsia::modular::auth::AccountProviderContext,
                    fuchsia::ui::policy::KeyboardCaptureListenerHACK,
                    modular::UserProviderImpl::Delegate {
 public:
  explicit BasemgrImpl(const modular::BasemgrSettings& settings,
                       std::shared_ptr<component::StartupContext> context,
                       std::function<void()> on_shutdown);

  ~BasemgrImpl() override;

 private:
  void InitializePresentation(
      fidl::InterfaceHandle<fuchsia::ui::viewsv1token::ViewOwner> view_owner);

  void StartBaseShell();

  FuturePtr<> StopBaseShell();

  FuturePtr<> StopAccountProvider();

  FuturePtr<> StopTokenManagerFactoryApp();

  void Start();

  // |fuchsia::modular::BaseShellContext|
  void GetUserProvider(
      fidl::InterfaceRequest<fuchsia::modular::UserProvider> request) override;

  // |fuchsia::modular::BaseShellContext|
  void Shutdown() override;

  // |AccountProviderContext|
  void GetAuthenticationContext(
      fidl::StringPtr account_id,
      fidl::InterfaceRequest<fuchsia::modular::auth::AuthenticationContext>
          request) override;

  // |AuthenticationContextProvider|
  void GetAuthenticationUIContext(
      fidl::InterfaceRequest<fuchsia::auth::AuthenticationUIContext> request)
      override;

  // |UserProviderImpl::Delegate|
  void DidLogin() override;

  // |UserProviderImpl::Delegate|
  void DidLogout() override;

  // |UserProviderImpl::Delegate|
  fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
      GetSessionShellViewOwner(
          fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>)
          override;

  // |UserProviderImpl::Delegate|
  fidl::InterfaceHandle<fuchsia::sys::ServiceProvider>
      GetSessionShellServiceProvider(
          fidl::InterfaceHandle<fuchsia::sys::ServiceProvider>) override;

  // |KeyboardCaptureListenerHACK|
  void OnEvent(fuchsia::ui::input::KeyboardEvent event) override;

  void AddGlobalKeyboardShortcuts(
      fuchsia::ui::policy::PresentationPtr& presentation);

  void UpdatePresentation(const SessionShellSettings& settings);

  void SwapSessionShell();

  void SetNextShadowTechnique();

  void SetShadowTechnique(fuchsia::ui::gfx::ShadowTechnique shadow_technique);

  void ToggleClipping();

  const modular::BasemgrSettings& settings_;  // Not owned nor copied.

  AsyncHolder<UserProviderImpl> user_provider_impl_;

  std::shared_ptr<component::StartupContext> const context_;
  fuchsia::modular::BasemgrMonitorPtr monitor_;
  std::function<void()> on_shutdown_;

  fidl::Binding<fuchsia::modular::BaseShellContext> base_shell_context_binding_;
  fidl::Binding<fuchsia::modular::auth::AccountProviderContext>
      account_provider_context_binding_;
  fidl::Binding<fuchsia::auth::AuthenticationContextProvider>
      authentication_context_provider_binding_;

  std::unique_ptr<AppClient<fuchsia::modular::auth::AccountProvider>>
      account_provider_;
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>>
      token_manager_factory_app_;
  fuchsia::auth::TokenManagerFactoryPtr token_manager_factory_;

  bool base_shell_running_{};
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> base_shell_app_;
  fuchsia::modular::BaseShellPtr base_shell_;

  fidl::BindingSet<fuchsia::ui::policy::KeyboardCaptureListenerHACK>
      keyboard_capture_listener_bindings_;

  fuchsia::ui::viewsv1token::ViewOwnerPtr session_shell_view_owner_;

  struct {
    fuchsia::ui::policy::PresentationPtr presentation;
    fidl::BindingSet<fuchsia::ui::policy::Presentation> bindings;

    fuchsia::ui::gfx::ShadowTechnique shadow_technique =
        fuchsia::ui::gfx::ShadowTechnique::UNSHADOWED;
    bool clipping_enabled{};
  } presentation_state_;

  component::ServiceNamespace service_namespace_;

  std::vector<SessionShellSettings>::size_type active_session_shell_index_{};

  FXL_DISALLOW_COPY_AND_ASSIGN(BasemgrImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_BASEMGR_BASEMGR_IMPL_H_

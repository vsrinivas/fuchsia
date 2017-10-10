// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_DEVICE_RUNNER_USER_CONTROLLER_IMPL_H_
#define PERIDOT_BIN_DEVICE_RUNNER_USER_CONTROLLER_IMPL_H_

#include "lib/app/cpp/application_context.h"
#include "lib/app/fidl/application_environment.fidl.h"
#include "lib/auth/fidl/account/account.fidl.h"
#include "lib/auth/fidl/account_provider.fidl.h"
#include "lib/config/fidl/config.fidl.h"
#include "lib/device/fidl/user_provider.fidl.h"
#include "lib/fidl/cpp/bindings/array.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fidl/cpp/bindings/interface_handle.h"
#include "lib/fidl/cpp/bindings/interface_ptr_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fxl/macros.h"
#include "lib/ui/views/fidl/view_token.fidl.h"
#include "lib/user/fidl/user_runner.fidl.h"
#include "peridot/lib/fidl/app_client.h"
#include "peridot/lib/fidl/scope.h"

namespace modular {

// |UserControllerImpl| starts and manages a UserRunner. The life time of a
// UserRunner is bound to this class.  |UserControllerImpl| is not self-owned,
// but still drives its own deletion: On logout, it signals its
// owner (DeviceRunnerApp) to delete it.
class UserControllerImpl : UserController, UserContext {
 public:
  // After perfoming logout, to signal our completion (and deletion of our
  // instance) to our owner, we do it using a callback supplied to us in our
  // constructor. (The alternative is to take in a DeviceRunnerApp*, which seems
  // a little specific and overscoped).
  using DoneCallback = std::function<void(UserControllerImpl*)>;

  UserControllerImpl(
      app::ApplicationLauncher* application_launcher,
      AppConfigPtr user_runner,
      AppConfigPtr user_shell,
      AppConfigPtr story_shell,
      fidl::InterfaceHandle<auth::TokenProviderFactory> token_provider_factory,
      auth::AccountPtr account,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceHandle<app::ServiceProvider> device_shell_services,
      fidl::InterfaceRequest<UserController> user_controller_request,
      DoneCallback done);

  // This will effectively tear down the entire instance by calling |done|.
  // |UserController|
  void Logout(const LogoutCallback& done) override;

 private:
  // |UserController|
  void Watch(fidl::InterfaceHandle<UserWatcher> watcher) override;

  // |UserContext|
  void Logout() override;

  // |UserContext|
  void GetPresentation(
      fidl::InterfaceRequest<mozart::Presentation> presentation) override;

  std::unique_ptr<Scope> user_runner_scope_;
  std::unique_ptr<AppClient<UserRunner>> user_runner_;

  fidl::Binding<UserContext> user_context_binding_;
  fidl::Binding<UserController> user_controller_binding_;

  fidl::InterfacePtrSet<modular::UserWatcher> user_watchers_;

  std::vector<LogoutCallback> logout_response_callbacks_;

  app::ServiceProviderPtr device_shell_services_;

  DoneCallback done_;

  FXL_DISALLOW_COPY_AND_ASSIGN(UserControllerImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_DEVICE_RUNNER_USER_CONTROLLER_IMPL_H_

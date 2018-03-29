// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_DEVICE_RUNNER_USER_CONTROLLER_IMPL_H_
#define PERIDOT_BIN_DEVICE_RUNNER_USER_CONTROLLER_IMPL_H_

#include <fuchsia/cpp/component.h>
#include <fuchsia/cpp/modular.h>
#include <fuchsia/cpp/modular_auth.h>
#include <fuchsia/cpp/modular_private.h>
#include <fuchsia/cpp/presentation.h>
#include <fuchsia/cpp/views_v1_token.h>
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/array.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fidl/cpp/interface_handle.h"
#include "lib/fidl/cpp/interface_ptr_set.h"
#include "lib/fidl/cpp/interface_request.h"
#include "lib/fxl/macros.h"
#include "peridot/lib/fidl/app_client.h"
#include "peridot/lib/fidl/scope.h"

namespace modular {

// |UserControllerImpl| starts and manages a UserRunner. The life time of a
// UserRunner is bound to this class.  |UserControllerImpl| is not self-owned,
// but still drives its own deletion: On logout, it signals its
// owner (DeviceRunnerApp) to delete it.
class UserControllerImpl : UserController, modular_private::UserContext {
 public:
  // After perfoming logout, to signal our completion (and deletion of our
  // instance) to our owner, we do it using a callback supplied to us in our
  // constructor. (The alternative is to take in a DeviceRunnerApp*, which seems
  // a little specific and overscoped).
  using DoneCallback = std::function<void(UserControllerImpl*)>;

  UserControllerImpl(
      component::ApplicationLauncher* application_launcher,
      AppConfig user_runner,
      AppConfig user_shell,
      AppConfig story_shell,
      fidl::InterfaceHandle<modular_auth::TokenProviderFactory>
          token_provider_factory,
      modular_auth::AccountPtr account,
      fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request,
      fidl::InterfaceHandle<component::ServiceProvider> device_shell_services,
      fidl::InterfaceRequest<UserController> user_controller_request,
      DoneCallback done);

  std::string DumpState();

  // This will effectively tear down the entire instance by calling |done|.
  // |UserController|
  void Logout(LogoutCallback done) override;

 private:
  // |UserController|
  void SwapUserShell(AppConfig user_shell,
                     SwapUserShellCallback callback) override;

  // |UserController|
  void Watch(fidl::InterfaceHandle<UserWatcher> watcher) override;

  // |UserContext|
  void Logout() override;

  // |UserContext|
  void GetPresentation(
      fidl::InterfaceRequest<presentation::Presentation> presentation) override;

  std::unique_ptr<Scope> user_runner_scope_;
  std::unique_ptr<AppClient<Lifecycle>> user_runner_app_;
  modular_private::UserRunnerPtr user_runner_;

  fidl::Binding<UserContext> user_context_binding_;
  fidl::Binding<UserController> user_controller_binding_;

  fidl::InterfacePtrSet<modular::UserWatcher> user_watchers_;

  std::vector<LogoutCallback> logout_response_callbacks_;

  component::ServiceProviderPtr device_shell_services_;

  DoneCallback done_;

  FXL_DISALLOW_COPY_AND_ASSIGN(UserControllerImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_DEVICE_RUNNER_USER_CONTROLLER_IMPL_H_

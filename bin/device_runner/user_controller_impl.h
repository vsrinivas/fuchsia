// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_DEVICE_RUNNER_USER_CONTROLLER_IMPL_H_
#define PERIDOT_BIN_DEVICE_RUNNER_USER_CONTROLLER_IMPL_H_

#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <fuchsia/modular/internal/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views_v1_token/cpp/fidl.h>

#include "lib/app/cpp/startup_context.h"
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
class UserControllerImpl : fuchsia::modular::UserController,
                           fuchsia::modular::internal::UserContext {
 public:
  // After perfoming logout, to signal our completion (and deletion of our
  // instance) to our owner, we do it using a callback supplied to us in our
  // constructor. (The alternative is to take in a DeviceRunnerApp*, which seems
  // a little specific and overscoped).
  using DoneCallback = std::function<void(UserControllerImpl*)>;

  UserControllerImpl(
      fuchsia::sys::Launcher* launcher, fuchsia::modular::AppConfig user_runner,
      fuchsia::modular::AppConfig user_shell,
      fuchsia::modular::AppConfig story_shell,
      fidl::InterfaceHandle<fuchsia::modular::auth::TokenProviderFactory>
          token_provider_factory,
      fuchsia::modular::auth::AccountPtr account,
      fidl::InterfaceRequest<fuchsia::ui::views_v1_token::ViewOwner>
          view_owner_request,
      fidl::InterfaceHandle<fuchsia::sys::ServiceProvider>
          device_shell_services,
      fidl::InterfaceRequest<fuchsia::modular::UserController>
          user_controller_request,
      DoneCallback done);

  // This will effectively tear down the entire instance by calling |done|.
  // |fuchsia::modular::UserController|
  void Logout(LogoutCallback done) override;

 private:
  // |fuchsia::modular::UserController|
  void SwapUserShell(fuchsia::modular::AppConfig user_shell,
                     SwapUserShellCallback callback) override;

  // |fuchsia::modular::UserController|
  void Watch(
      fidl::InterfaceHandle<fuchsia::modular::UserWatcher> watcher) override;

  // |UserContext|
  void Logout() override;

  // |UserContext|
  void GetPresentation(fidl::InterfaceRequest<fuchsia::ui::policy::Presentation>
                           request) override;

  std::unique_ptr<Scope> user_runner_scope_;
  std::unique_ptr<AppClient<fuchsia::modular::Lifecycle>> user_runner_app_;
  fuchsia::modular::internal::UserRunnerPtr user_runner_;

  fidl::Binding<fuchsia::modular::internal::UserContext> user_context_binding_;
  fidl::Binding<fuchsia::modular::UserController> user_controller_binding_;

  fidl::InterfacePtrSet<fuchsia::modular::UserWatcher> user_watchers_;

  std::vector<LogoutCallback> logout_response_callbacks_;

  fuchsia::sys::ServiceProviderPtr device_shell_services_;

  DoneCallback done_;

  FXL_DISALLOW_COPY_AND_ASSIGN(UserControllerImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_DEVICE_RUNNER_USER_CONTROLLER_IMPL_H_

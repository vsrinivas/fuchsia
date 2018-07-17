// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/device_runner/user_controller_impl.h"

#include <memory>
#include <utility>

#include <lib/fidl/cpp/synchronous_interface_ptr.h>

#include "peridot/lib/common/async_holder.h"
#include "peridot/lib/common/names.h"
#include "peridot/lib/common/teardown.h"
#include "peridot/lib/fidl/array_to_string.h"

namespace modular {

UserControllerImpl::UserControllerImpl(
    fuchsia::sys::Launcher* const launcher,
    fuchsia::modular::AppConfig user_runner,
    fuchsia::modular::AppConfig user_shell,
    fuchsia::modular::AppConfig story_shell,
    fidl::InterfaceHandle<fuchsia::modular::auth::TokenProviderFactory>
        token_provider_factory,
    fuchsia::modular::auth::AccountPtr account,
    fidl::InterfaceRequest<fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request,
    fidl::InterfaceHandle<fuchsia::sys::ServiceProvider> device_shell_services,
    fidl::InterfaceRequest<fuchsia::modular::UserController>
        user_controller_request,
    DoneCallback done)
    : user_context_binding_(this),
      user_controller_binding_(this, std::move(user_controller_request)),
      device_shell_services_(
          device_shell_services ? device_shell_services.Bind() : nullptr),
      done_(std::move(done)) {
  // 0. Generate the path to map '/data' for the user runner we are starting.
  std::string data_origin;
  if (!account) {
    // Guest user.
    // Generate a random number to be used in this case.
    uint32_t random_number = 0;
    zx_cprng_draw(&random_number, sizeof random_number);
    data_origin = std::string("/data/modular/USER_GUEST_") +
                  std::to_string(random_number);
  } else {
    // Non-guest user.
    data_origin = std::string("/data/modular/USER_") + std::string(account->id);
  }

  FXL_LOG(INFO) << "USER RUNNER DATA ORIGIN IS " << data_origin;

  // 1. Launch UserRunner in the current environment.
  user_runner_app_ = std::make_unique<AppClient<fuchsia::modular::Lifecycle>>(
      launcher, std::move(user_runner), data_origin);

  // 2. Initialize the UserRunner service.
  user_runner_app_->services().ConnectToService(user_runner_.NewRequest());
  user_runner_->Initialize(
      std::move(account), std::move(user_shell), std::move(story_shell),
      std::move(token_provider_factory), user_context_binding_.NewBinding(),
      std::move(view_owner_request));
}

// |fuchsia::modular::UserController|
void UserControllerImpl::Logout(LogoutCallback done) {
  FXL_LOG(INFO) << "fuchsia::modular::UserController::Logout()";
  logout_response_callbacks_.push_back(done);
  if (logout_response_callbacks_.size() > 1) {
    return;
  }

  // This should prevent us from receiving any further requests.
  user_controller_binding_.Unbind();
  user_context_binding_.Unbind();

  user_runner_app_->Teardown(kUserRunnerTimeout, [this] {
    for (const auto& done : logout_response_callbacks_) {
      done();
    }
    // We announce |OnLogout| only at point just before deleting ourselves,
    // so we can avoid any race conditions that may be triggered by |Shutdown|
    // (which in-turn will call this |Logout| since we have not completed yet).
    for (auto& watcher : user_watchers_.ptrs()) {
      (*watcher)->OnLogout();
    }
    done_(this);
  });
}

// |UserContext|
void UserControllerImpl::GetPresentation(
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) {
  if (device_shell_services_) {
    device_shell_services_->ConnectToService(kPresentationService,
                                             request.TakeChannel());
  }
}

FuturePtr<> UserControllerImpl::SwapUserShell(
    fuchsia::modular::AppConfig user_shell_config) {
  auto future = Future<>::Create("SwapUserShell");
  SwapUserShell(std::move(user_shell_config), future->Completer());
  return future;
}

// |fuchsia::modular::UserController|
void UserControllerImpl::SwapUserShell(
    fuchsia::modular::AppConfig user_shell_config,
    SwapUserShellCallback callback) {
  user_runner_->SwapUserShell(std::move(user_shell_config), callback);
}

// |fuchsia::modular::UserController|
void UserControllerImpl::Watch(
    fidl::InterfaceHandle<fuchsia::modular::UserWatcher> watcher) {
  user_watchers_.AddInterfacePtr(watcher.Bind());
}

// |UserContext|
// TODO(alhaad): Reconcile UserContext.Logout() and UserControllerImpl.Logout().
void UserControllerImpl::Logout() {
  FXL_LOG(INFO) << "UserContext::Logout()";
  Logout([] {});
}

}  // namespace modular

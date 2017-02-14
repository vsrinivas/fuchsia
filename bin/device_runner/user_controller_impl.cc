// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/src/device_runner/user_controller_impl.h"

#include <memory>
#include <utility>

#include "application/lib/app/connect.h"
#include "apps/modular/lib/fidl/array_to_string.h"

namespace modular {
namespace {

constexpr char kUserScopeLabelPrefix[] = "user-";

}  // namespace

UserControllerImpl::UserControllerImpl(
    std::shared_ptr<app::ApplicationContext> app_context,
    const std::string& user_runner,
    const std::string& user_shell,
    const std::vector<std::string>& user_shell_args,
    fidl::Array<uint8_t> user_id,
    fidl::InterfaceHandle<ledger::LedgerRepository> ledger_repository,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    fidl::InterfaceRequest<UserController> user_controller,
    DoneCallback done)
    : user_context_impl_(this),
      user_context_binding_(&user_context_impl_),
      user_controller_binding_(this, std::move(user_controller)),
      done_(done) {
  const std::string label = kUserScopeLabelPrefix + to_hex_string(user_id);

  // 1. Create a child environment for the UserRunner.
  app::ApplicationEnvironmentPtr env;
  app_context->environment()->Duplicate(env.NewRequest());
  user_runner_scope_ = std::make_unique<Scope>(std::move(env), label);

  app::ApplicationLauncherPtr launcher;
  user_runner_scope_->environment()->GetApplicationLauncher(
      launcher.NewRequest());

  // 2. Launch UserRunner in the new environment.
  auto launch_info = app::ApplicationLaunchInfo::New();
  launch_info->url = user_runner;
  app::ServiceProviderPtr services;
  launch_info->services = services.NewRequest();
  launcher->CreateApplication(std::move(launch_info),
                              user_runner_controller_.NewRequest());

  // 3. Initialize the UserRunner service.
  UserRunnerFactoryPtr user_runner_factory;
  app::ConnectToService(services.get(), user_runner_factory.NewRequest());
  user_runner_factory->Create(
      std::move(user_id), user_shell, to_array(user_shell_args),
      std::move(ledger_repository), user_context_binding_.NewBinding(),
      std::move(view_owner_request), user_runner_.NewRequest());
}

// |UserController|
void UserControllerImpl::Logout(const LogoutCallback& done) {
  logout_response_callbacks_.push_back(done);
  if (logout_response_callbacks_.size() > 1) {
    return;
  }

  // This should prevent us from receiving any further requests.
  user_controller_binding_.Unbind();
  user_context_binding_.Unbind();

  user_runner_->Terminate([this, done] {
    for (const auto& done_cb : logout_response_callbacks_) {
      done_cb();
    }
    // We announce |OnLogout| only at point just before deleting ourselves,
    // so we can avoid any race conditions that may be triggered by |Shutdown|
    // (which in-turn will call this |Logout| since we have not completed yet).
    user_watchers_.ForAllPtrs(
        [](UserWatcher* watcher) { watcher->OnLogout(); });
    done_();
  });
}

// |UserController|
void UserControllerImpl::Watch(fidl::InterfaceHandle<UserWatcher> watcher) {
  user_watchers_.AddInterfacePtr(UserWatcherPtr::Create(std::move(watcher)));
}

// |UserContext|
void UserContextImpl::Logout() {
  controller_->Logout([] {});
}

}  // namespace modular

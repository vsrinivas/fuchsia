// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MODULAR_SRC_DEVICE_RUNNER_USER_PROVIDER_IMPL_H_
#define APPS_MODULAR_SRC_DEVICE_RUNNER_USER_PROVIDER_IMPL_H_

#include "application/lib/app/application_context.h"
#include "apps/ledger/services/internal/internal.fidl.h"
#include "apps/modular/services/auth/account_provider.fidl.h"
#include "apps/modular/services/config/config.fidl.h"
#include "apps/modular/services/device/user_provider.fidl.h"
#include "apps/modular/src/device_runner/user_controller_impl.h"
#include "apps/mozart/services/views/view_token.fidl.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/fidl/cpp/bindings/interface_request.h"

namespace modular {

struct UsersStorage;

class UserProviderImpl : UserProvider {
 public:
  UserProviderImpl(std::shared_ptr<app::ApplicationContext> app_context,
                   const AppConfig& default_user_shell,
                   const AppConfig& story_shell,
                   ledger::LedgerRepositoryFactory* ledger_repository_factory,
                   bool ledger_repository_for_testing,
                   auth::AccountProvider* account_provider);

  void Connect(fidl::InterfaceRequest<UserProvider> request);

  void Teardown(const std::function<void()>& callback);

 private:
  // |UserProvider|
  void Login(UserLoginParamsPtr user_login_params) override;

  // |UserProvider|
  void PreviousUsers(const PreviousUsersCallback& callback) override;

  // |UserProvider|
  void AddUser(auth::IdentityProvider identity_provider,
               const AddUserCallback& callback) override;

  // |UserProvider|
  void RemoveUser(const fidl::String& account_id) override;

  bool WriteUsersDb(const std::string& serialized_users, std::string* error);
  bool Parse(const std::string& serialized_users);

  void LoginInternal(auth::AccountPtr account,
                     const std::string& local_ledger_path,
                     UserLoginParamsPtr user_login_params);

  fidl::BindingSet<UserProvider> bindings_;

  std::shared_ptr<app::ApplicationContext> app_context_;
  const AppConfig& default_user_shell_;  // Neither owned nor copied.
  const AppConfig& story_shell_;         // Neither owned nor copied.
  ledger::LedgerRepositoryFactory* const ledger_repository_factory_;
  const bool ledger_repository_for_testing_;
  auth::AccountProvider* const account_provider_;

  std::string serialized_users_;
  const modular::UsersStorage* users_storage_ = nullptr;

  std::unordered_map<UserControllerImpl*, std::unique_ptr<UserControllerImpl>>
      user_controllers_;

  FTL_DISALLOW_COPY_AND_ASSIGN(UserProviderImpl);
};

}  // namespace modular

#endif  // APPS_MODULAR_SRC_DEVICE_RUNNER_USER_PROVIDER_IMPL_H_

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_DEVICE_RUNNER_USER_PROVIDER_IMPL_H_
#define PERIDOT_BIN_DEVICE_RUNNER_USER_PROVIDER_IMPL_H_

#include <fuchsia/cpp/modular.h>
#include <fuchsia/cpp/modular_auth.h>
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fidl/cpp/interface_request.h"
#include "peridot/bin/device_runner/user_controller_impl.h"

namespace modular {

struct UsersStorage;

class UserProviderImpl : UserProvider {
 public:
  UserProviderImpl(std::shared_ptr<component::ApplicationContext> app_context,
                   const AppConfig& user_runner,
                   const AppConfig& default_user_shell,
                   const AppConfig& story_shell,
                   modular_auth::AccountProvider* account_provider);

  void Connect(fidl::InterfaceRequest<UserProvider> request);

  void Teardown(const std::function<void()>& callback);

  std::string DumpState();

 private:
  // |UserProvider|
  void Login(UserLoginParams params) override;

  // |UserProvider|
  void PreviousUsers(PreviousUsersCallback callback) override;

  // |UserProvider|
  void AddUser(modular_auth::IdentityProvider identity_provider,
               AddUserCallback callback) override;

  // |UserProvider|
  void RemoveUser(fidl::StringPtr account_id,
                  RemoveUserCallback callback) override;

  bool WriteUsersDb(const std::string& serialized_users, std::string* error);
  bool Parse(const std::string& serialized_users);

  void LoginInternal(modular_auth::AccountPtr account, UserLoginParams params);

  fidl::BindingSet<UserProvider> bindings_;

  std::shared_ptr<component::ApplicationContext> app_context_;
  const AppConfig& user_runner_;         // Neither owned nor copied.
  const AppConfig& default_user_shell_;  // Neither owned nor copied.
  const AppConfig& story_shell_;         // Neither owned nor copied.
  modular_auth::AccountProvider* const account_provider_;

  std::string serialized_users_;
  const modular::UsersStorage* users_storage_ = nullptr;

  std::map<UserControllerImpl*, std::unique_ptr<UserControllerImpl>>
      user_controllers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(UserProviderImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_DEVICE_RUNNER_USER_PROVIDER_IMPL_H_

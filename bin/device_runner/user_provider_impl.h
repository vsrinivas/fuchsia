// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_DEVICE_RUNNER_USER_PROVIDER_IMPL_H_
#define PERIDOT_BIN_DEVICE_RUNNER_USER_PROVIDER_IMPL_H_

#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <lib/app/cpp/startup_context.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>

#include "peridot/bin/device_runner/user_controller_impl.h"

namespace fuchsia {
namespace modular {
struct UsersStorage;
}
}  // namespace fuchsia

namespace modular {

class UserProviderImpl : fuchsia::modular::UserProvider {
 public:
  UserProviderImpl(std::shared_ptr<fuchsia::sys::StartupContext> context,
                   const fuchsia::modular::AppConfig& user_runner,
                   const fuchsia::modular::AppConfig& default_user_shell,
                   const fuchsia::modular::AppConfig& story_shell,
                   fuchsia::modular::auth::AccountProvider* account_provider);

  void Connect(fidl::InterfaceRequest<fuchsia::modular::UserProvider> request);

  void Teardown(const std::function<void()>& callback);

 private:
  // |fuchsia::modular::UserProvider|
  void Login(fuchsia::modular::UserLoginParams params) override;

  // |fuchsia::modular::UserProvider|
  void PreviousUsers(PreviousUsersCallback callback) override;

  // |fuchsia::modular::UserProvider|
  void AddUser(fuchsia::modular::auth::IdentityProvider identity_provider,
               AddUserCallback callback) override;

  // |fuchsia::modular::UserProvider|
  void RemoveUser(fidl::StringPtr account_id,
                  RemoveUserCallback callback) override;

  bool WriteUsersDb(const std::string& serialized_users, std::string* error);
  bool Parse(const std::string& serialized_users);

  void LoginInternal(fuchsia::modular::auth::AccountPtr account,
                     fuchsia::modular::UserLoginParams params);

  fidl::BindingSet<fuchsia::modular::UserProvider> bindings_;

  std::shared_ptr<fuchsia::sys::StartupContext> context_;
  const fuchsia::modular::AppConfig& user_runner_;  // Neither owned nor copied.
  const fuchsia::modular::AppConfig&
      default_user_shell_;                          // Neither owned nor copied.
  const fuchsia::modular::AppConfig& story_shell_;  // Neither owned nor copied.
  fuchsia::modular::auth::AccountProvider* const account_provider_;

  std::string serialized_users_;
  const fuchsia::modular::UsersStorage* users_storage_ = nullptr;

  std::map<UserControllerImpl*, std::unique_ptr<UserControllerImpl>>
      user_controllers_;

  FXL_DISALLOW_COPY_AND_ASSIGN(UserProviderImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_DEVICE_RUNNER_USER_PROVIDER_IMPL_H_

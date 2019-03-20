// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_BASEMGR_USER_PROVIDER_IMPL_H_
#define PERIDOT_BIN_BASEMGR_USER_PROVIDER_IMPL_H_

#include <fuchsia/auth/account/cpp/fidl.h>
#include <fuchsia/auth/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <lib/async/cpp/future.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>

#include "peridot/bin/basemgr/session_context_impl.h"

namespace fuchsia {
namespace modular {
struct UsersStorage;
}
}  // namespace fuchsia

namespace modular {

class UserProviderImpl : fuchsia::auth::AuthenticationContextProvider,
                         fuchsia::modular::UserProvider,
                         fuchsia::auth::account::AccountListener {
 public:
  // Called after UserProviderImpl successfully logs in a user.
  using OnLoginCallback =
      fit::function<void(fuchsia::modular::auth::AccountPtr account,
                         fuchsia::auth::TokenManagerPtr ledger_token_manager,
                         fuchsia::auth::TokenManagerPtr agent_token_manager)>;

  // All parameters must outlive UserProviderImpl.
  UserProviderImpl(
      fuchsia::auth::account::AccountManager* const account_manager,
      fuchsia::auth::TokenManagerFactory* const token_manager_factory,
      fuchsia::auth::AuthenticationContextProviderPtr auth_context_provider,
      OnLoginCallback on_login);

  void Connect(fidl::InterfaceRequest<fuchsia::modular::UserProvider> request);

  // |fuchsia::modular::UserProvider|, also called by |basemgr_impl|.
  void Login(fuchsia::modular::UserLoginParams params) override;

  // |fuchsia::modular::UserProvider|, also called by |basemgr_impl|.
  void PreviousUsers(PreviousUsersCallback callback) override;

  // Removes all the users in storage. |callback| is invoked after all users
  // have been removed.
  void RemoveAllUsers(fit::function<void()> callback);

 private:
  // |fuchsia::modular::UserProvider|
  void AddUser(fuchsia::modular::auth::IdentityProvider identity_provider,
               AddUserCallback callback) override;

  // |fuchsia::modular::UserProvider|
  void RemoveUser(std::string account_id, RemoveUserCallback callback) override;

  // |fuchsia::auth::AuthenticationContextProvider|
  void GetAuthenticationUIContext(
      fidl::InterfaceRequest<fuchsia::auth::AuthenticationUIContext> request)
      override;

  // Returns a new |fuchsia::auth::TokenManager| handle for the given user
  // account |account_id|.
  fuchsia::auth::TokenManagerPtr CreateTokenManager(std::string account_id);

  bool AddUserToAccountsDB(const fuchsia::modular::auth::Account* account,
                           std::string* error);
  bool RemoveUserFromAccountsDB(fidl::StringPtr account_id, std::string* error);
  bool WriteUsersDb(const std::string& serialized_users, std::string* error);
  bool Parse(const std::string& serialized_users);
  void RemoveUserInternal(fuchsia::modular::auth::AccountPtr account,
                          RemoveUserCallback callback);
  void LoginInternal(fuchsia::modular::auth::AccountPtr account,
                     fuchsia::modular::UserLoginParams params);

  // Currently AccountListener is only exercised by test code (see
  // dev_base_shell.cc). OnInitialize, user_provider_impl will try to start a
  // session with a guest modular account if there are already auth accounts on
  // the device. OnAccountAdded, user_provider_impl will try to start a session
  // with a a guest modular account.
  //
  // |fuchsia::auth::account::AccountListner|
  void OnInitialize(std::vector<fuchsia::auth::account::AccountAuthState>,
                    OnInitializeCallback) override;
  // |fuchsia::auth::account::AccountListner|
  void OnAccountAdded(fuchsia::auth::account::LocalAccountId,
                      OnAccountAddedCallback) override;
  // |fuchsia::auth::account::AccountListner|
  void OnAccountRemoved(fuchsia::auth::account::LocalAccountId,
                        OnAccountRemovedCallback) override;
  // |fuchsia::auth::account::AccountListner|
  void OnAuthStateChanged(fuchsia::auth::account::AccountAuthState,
                          OnAuthStateChangedCallback) override;

  fidl::BindingSet<fuchsia::modular::UserProvider> bindings_;

  fuchsia::auth::account::AccountManager* const
      account_manager_;  // Neither owned nor copied.
  fuchsia::auth::TokenManagerFactory* const
      token_manager_factory_;  // Neither owned nor copied.
  fuchsia::auth::AuthenticationContextProviderPtr
      authentication_context_provider_;

  fidl::Binding<fuchsia::auth::AuthenticationContextProvider>
      authentication_context_provider_binding_;
  fidl::Binding<fuchsia::auth::account::AccountListener>
      account_listener_binding_;
  std::string serialized_users_;
  const fuchsia::modular::UsersStorage* users_storage_ = nullptr;

  OnLoginCallback on_login_;

  FXL_DISALLOW_COPY_AND_ASSIGN(UserProviderImpl);
};

}  // namespace modular

#endif  // PERIDOT_BIN_BASEMGR_USER_PROVIDER_IMPL_H_

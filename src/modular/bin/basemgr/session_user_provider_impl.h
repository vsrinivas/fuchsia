// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_BASEMGR_SESSION_USER_PROVIDER_IMPL_H_
#define SRC_MODULAR_BIN_BASEMGR_SESSION_USER_PROVIDER_IMPL_H_

#include <fuchsia/auth/cpp/fidl.h>
#include <fuchsia/identity/account/cpp/fidl.h>
#include <fuchsia/modular/auth/cpp/fidl.h>
#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/interface_request.h>

#include "src/modular/lib/async/cpp/future.h"

namespace modular {

// This class manages the session-to-persona mapping (which personas are
// participating in which sessions).
//
// The current policy is to automatically login every newly added account's
// default persona into a new session. Whether a new session gets started or not
// is up to |session_provider_impl|.
class SessionUserProviderImpl : fuchsia::auth::AuthenticationContextProvider,
                                fuchsia::modular::UserProvider,
                                fuchsia::identity::account::AccountListener {
 public:
  // Called after SessionUserProviderImpl successfully registers as an account
  // listener.
  using OnInitializeCallback = fit::function<void()>;

  // Called after SessionUserProviderImpl successfully logs in a user.
  using OnLoginCallback = fit::function<void(fuchsia::modular::auth::AccountPtr account,
                                             fuchsia::auth::TokenManagerPtr agent_token_manager)>;

  // |account_manager| Used to register SessionUserProviderImpl as an
  // |AccountListener| to receive updates on newly added/removed accounts. Must
  // be present, and must outlive this instance.
  //
  // |token_manager_factory| Used to vend token managers for guest login, in
  // which no account is created. Must be present, and must outlive this
  // instance.
  //
  // |auth_context_provider| Used to forward authentication UI requests from
  // auth to the base shell. Must be present.
  //
  // |on_initialize| Callback invoked when |AccountManager| has initialized with
  // initial data. Must be present.
  //
  // |on_login| Callback  invoked when a persona is ready to be logged into a
  // new session. Must be present.
  SessionUserProviderImpl(fuchsia::identity::account::AccountManager* const account_manager,
                          fuchsia::auth::TokenManagerFactory* const token_manager_factory,
                          fuchsia::auth::AuthenticationContextProviderPtr auth_context_provider,
                          OnInitializeCallback on_initialize, OnLoginCallback on_login);

  void Connect(fidl::InterfaceRequest<fuchsia::modular::UserProvider> request);

  // |fuchsia::modular::UserProvider|, also called by |basemgr_impl|.
  void Login(fuchsia::modular::UserLoginParams params) override;

  // |fuchsia::modular::UserProvider|, also called by |basemgr_impl|.
  void Login2(fuchsia::modular::UserLoginParams2 params) override;

  void RemoveAllUsers(fit::function<void()> callback);

 private:
  // |fuchsia::modular::UserProvider|
  void AddUser(fuchsia::modular::auth::IdentityProvider identity_provider,
               AddUserCallback callback) override;

  // |fuchsia::modular::UserProvider|
  void RemoveUser(std::string account_id, RemoveUserCallback callback) override;

  // |fuchsia::modular::UserProvider|, also called by |basemgr_impl|.
  void PreviousUsers(PreviousUsersCallback callback) override;

  // |fuchsia::auth::AuthenticationContextProvider|
  void GetAuthenticationUIContext(
      fidl::InterfaceRequest<fuchsia::auth::AuthenticationUIContext> request) override;

  // Returns a new |fuchsia::auth::TokenManager| handle for the given user
  // account |account_id|.
  fuchsia::auth::TokenManagerPtr CreateTokenManager(std::string account_id);

  // OnInitialize, session_user_provider_impl will invoke |on_initialize_|.
  // OnAccountAdded, session_user_provider_impl will call |on_login_|.
  //
  // |fuchsia::identity::account::AccountListner|
  void OnInitialize(std::vector<fuchsia::identity::account::InitialAccountState>,
                    OnInitializeCallback) override;
  // |fuchsia::identity::account::AccountListner|
  void OnAccountAdded(fuchsia::identity::account::InitialAccountState,
                      OnAccountAddedCallback) override;
  // |fuchsia::identity::account::AccountListner|
  void OnAccountRemoved(uint64_t, OnAccountRemovedCallback) override;
  // |fuchsia::identity::account::AccountListner|
  void OnAuthStateChanged(fuchsia::identity::account::AccountAuthState,
                          OnAuthStateChangedCallback) override;

  fidl::BindingSet<fuchsia::modular::UserProvider> bindings_;

  fuchsia::identity::account::AccountManager* const account_manager_;  // Neither owned nor copied.
  fuchsia::auth::TokenManagerFactory* const token_manager_factory_;    // Neither owned nor copied.
  fuchsia::auth::AuthenticationContextProviderPtr authentication_context_provider_;

  fidl::Binding<fuchsia::auth::AuthenticationContextProvider>
      authentication_context_provider_binding_;
  fidl::Binding<fuchsia::identity::account::AccountListener> account_listener_binding_;

  // The personas that are currently, or should be, joined on the session that's
  // started in modular framework.
  struct JoinedPersona {
    // The persona joined on the session.
    fuchsia::identity::account::PersonaPtr persona;

    // The account associated with the above persona.
    fuchsia::identity::account::AccountPtr account;
  };
  std::vector<JoinedPersona> joined_personas_;

  OnInitializeCallback on_initialize_;
  OnLoginCallback on_login_;

  fxl::WeakPtrFactory<SessionUserProviderImpl> weak_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SessionUserProviderImpl);
};

}  // namespace modular

#endif  // SRC_MODULAR_BIN_BASEMGR_SESSION_USER_PROVIDER_IMPL_H_

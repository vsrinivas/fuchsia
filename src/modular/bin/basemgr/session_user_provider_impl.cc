// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/session_user_provider_impl.h"

#include <lib/fit/function.h>
#include <zircon/status.h>

#include "src/lib/syslog/cpp/logger.h"

namespace modular {

SessionUserProviderImpl::SessionUserProviderImpl(
    fuchsia::identity::account::AccountManager* const account_manager,
    fuchsia::auth::AuthenticationContextProviderPtr authentication_context_provider,
    OnInitializeCallback on_initialize, OnLoginCallback on_login)
    : account_manager_(account_manager),
      authentication_context_provider_(std::move(authentication_context_provider)),
      authentication_context_provider_binding_(this),
      account_listener_binding_(this),
      on_initialize_(std::move(on_initialize)),
      on_login_(std::move(on_login)),
      weak_factory_(this) {
  FX_CHECK(account_manager_);
  FX_CHECK(authentication_context_provider_);
  FX_CHECK(on_initialize_);
  FX_CHECK(on_login_);

  authentication_context_provider_binding_.set_error_handler([this](zx_status_t status) {
    FX_LOGS(WARNING) << "AuthenticationContextProvider disconnected.";
    authentication_context_provider_binding_.Unbind();
  });

  // Register SessionUserProvider as an AccountListener. All added accounts will
  // be logged in to a session.
  account_listener_binding_.set_error_handler([](zx_status_t status) {
    FX_LOGS(FATAL) << "AccountListener disconnected with status: " << zx_status_get_string(status);
  });

  fuchsia::identity::account::AccountListenerOptions options;
  options.initial_state = true;
  options.add_account = true;
  account_manager_->RegisterAccountListener(
      account_listener_binding_.NewBinding(), std::move(options),
      [weak_this = weak_factory_.GetWeakPtr()](auto result) {
        if (!weak_this) {
          return;
        }
        if (result.is_response()) {
          FX_LOGS(INFO) << "AccountListener registered.";
        } else {
          FX_LOGS(FATAL) << "AccountListener registration failed with status: "
                         << (uint32_t)result.err();
        }
      });
}

void SessionUserProviderImpl::Connect(
    fidl::InterfaceRequest<fuchsia::modular::UserProvider> request) {
  bindings_.AddBinding(this, std::move(request));
}

void SessionUserProviderImpl::AddUser(fuchsia::modular::auth::IdentityProvider identity_provider,
                                      AddUserCallback callback) {
  FX_LOGS(INFO) << "AddUser(IDP) is not implemented yet.";
    callback(nullptr, "Provision new account from auth provider not supported.");
}

void SessionUserProviderImpl::Login(fuchsia::modular::UserLoginParams params) {
  Login2(fuchsia::modular::UserLoginParams2{
      .account_id = std::move(params.account_id),
  });
}

void SessionUserProviderImpl::Login2(fuchsia::modular::UserLoginParams2 params) {
  bool login_as_guest = params.account_id.value_or("") == "";
  if (login_as_guest) {
    FX_LOGS(INFO) << "fuchsia::modular::UserProvider::Login() Login as guest";
    on_login_(/* account= */ nullptr);
  } else {
    FX_LOGS(INFO) << "fuchsia::modular::UserProvider::Login() Login as "
                     "authenticated user";

    auto account_deprecated = fuchsia::modular::auth::Account::New();
    account_deprecated->id = params.account_id->c_str();

    on_login_(std::move(account_deprecated));
  }
}

void SessionUserProviderImpl::RemoveAllUsers(fit::function<void()> callback) {
  account_manager_->GetAccountIds(
      [weak_this = weak_factory_.GetWeakPtr(),
       callback = std::move(callback)](std::vector<uint64_t> account_ids) mutable {
        if (!weak_this) {
          return;
        }
        if (account_ids.empty()) {
          callback();
          return;
        }

        // We only expect there to be one account at most.
        weak_this->account_manager_->RemoveAccount(
            account_ids.at(0), true, /* Force account removal */
            [callback = std::move(callback)](auto) { callback(); });
      });
}

void SessionUserProviderImpl::RemoveUser(std::string account_id, RemoveUserCallback callback) {
  FX_LOGS(INFO) << "RemoveUser() is not implemented yet.";
  callback("");
}

void SessionUserProviderImpl::PreviousUsers(PreviousUsersCallback callback) {
  FX_LOGS(INFO) << "PreviousUsers() is not implemented yet";
  std::vector<::fuchsia::modular::auth::Account> users;
  callback(std::move(users));
}

void SessionUserProviderImpl::GetAuthenticationUIContext(
    fidl::InterfaceRequest<fuchsia::auth::AuthenticationUIContext> request) {
  authentication_context_provider_->GetAuthenticationUIContext(std::move(request));
}

void SessionUserProviderImpl::OnInitialize(
    std::vector<fuchsia::identity::account::InitialAccountState>, OnInitializeCallback callback) {
  callback();
  on_initialize_();
}

void SessionUserProviderImpl::OnAccountAdded(
    fuchsia::identity::account::InitialAccountState account_state,
    OnAccountAddedCallback callback) {
  // TODO(MF-311): Get rid of this once clients of UserProvider interface start
  // using AccountManager.
  fuchsia::modular::UserLoginParams params;
  params.account_id = std::to_string(account_state.account_id);
  // Base shell may also call Login with the newly added account, but the Login
  // flow should be resilient to multiple invocations.
  Login(std::move(params));
  callback();
}

void SessionUserProviderImpl::OnAccountRemoved(uint64_t, OnAccountRemovedCallback callback) {
  callback();
}

void SessionUserProviderImpl::OnAuthStateChanged(fuchsia::identity::account::AccountAuthState,
                                                 OnAuthStateChangedCallback callback) {
  callback();
}

}  // namespace modular

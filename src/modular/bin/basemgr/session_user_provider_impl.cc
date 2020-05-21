// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/basemgr/session_user_provider_impl.h"

#include <lib/fit/function.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/status.h>

namespace modular {

SessionUserProviderImpl::SessionUserProviderImpl(OnLoginCallback on_login)
    : on_login_(std::move(on_login)), weak_factory_(this) {
  FX_CHECK(on_login_);
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
  bool is_ephemeral_account = params.account_id.value_or("") == "";
  Login3(is_ephemeral_account);
}

void SessionUserProviderImpl::Login2(fuchsia::modular::UserLoginParams2 params) {
  bool is_ephemeral_account = params.account_id.value_or("") == "";
  Login3(is_ephemeral_account);
}

void SessionUserProviderImpl::Login3(bool is_ephemeral_account) {
  if (is_ephemeral_account) {
    FX_LOGS(INFO) << "fuchsia::modular::UserProvider::Login() Login as ephemeral";
  } else {
    FX_LOGS(INFO) << "fuchsia::modular::UserProvider::Login() Login as persistant";
  }
  on_login_(is_ephemeral_account);
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

}  // namespace modular

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/firebase_auth/testing/test_token_manager.h"

#include <fuchsia/auth/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fidl/cpp/optional.h>
#include <src/lib/fxl/logging.h>

namespace firebase_auth {
TestTokenManager::TestTokenManager(async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher) {
  status_to_return_ = fuchsia::auth::Status::OK;
}

TestTokenManager::~TestTokenManager() {}

void TestTokenManager::Authorize(
    AppConfig app_config,
    fidl::InterfaceHandle<AuthenticationUIContext> auth_ui_context,
    std::vector<std::string> /*app_scopes*/,
    fidl::StringPtr /*user_profile_id*/, fidl::StringPtr /*auth_code*/,
    AuthorizeCallback callback /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

void TestTokenManager::GetAccessToken(
    AppConfig app_config, std::string /*user_profile_id*/,
    std::vector<std::string> /*app_scopes*/,
    GetAccessTokenCallback callback /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

void TestTokenManager::GetIdToken(AppConfig app_config,
                                  std::string /*user_profile_id*/,
                                  fidl::StringPtr /*audience*/,
                                  GetIdTokenCallback callback /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

void TestTokenManager::GetFirebaseToken(AppConfig /*app_config*/,
                                        std::string /*user_profile_id*/,
                                        std::string /*audience*/,
                                        std::string /*firebase_api_key*/,
                                        GetFirebaseTokenCallback callback) {
  fuchsia::auth::FirebaseTokenPtr token_to_return_copy;
  fidl::Clone(token_to_return_, &token_to_return_copy);
  fuchsia::auth::Status status_to_return_copy;
  status_to_return_copy = status_to_return_;
  async::PostTask(dispatcher_,
                  [status_to_return = status_to_return_copy,
                   token_to_return = std::move(token_to_return_copy),
                   callback = std::move(callback)]() mutable {
                    callback(status_to_return, std::move(token_to_return));
                  });
}

void TestTokenManager::DeleteAllTokens(AppConfig /*app_config*/,
                                       std::string /*user_profile_id*/,
                                       bool /*force*/,
                                       DeleteAllTokensCallback callback) {
  FXL_NOTIMPLEMENTED();
}

void TestTokenManager::ListProfileIds(AppConfig app_config,
                                      ListProfileIdsCallback callback) {
  FXL_NOTIMPLEMENTED();
}

void TestTokenManager::Set(std::string id_token, std::string local_id,
                           std::string email) {
  token_to_return_ = fuchsia::auth::FirebaseToken::New();
  token_to_return_->id_token = id_token;
  token_to_return_->local_id = local_id;
  token_to_return_->email = email;
  status_to_return_ = fuchsia::auth::Status::OK;
}

void TestTokenManager::SetError(fuchsia::auth::Status status) {
  FXL_CHECK(status != fuchsia::auth::Status::OK);
  token_to_return_ = nullptr;
  status_to_return_ = status;
}
}  // namespace firebase_auth

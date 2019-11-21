// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/firebase_auth/testing/fake_token_manager.h"

#include "src/ledger/lib/convert/convert.h"
#include "src/lib/fxl/logging.h"

namespace firebase_auth {

FakeTokenManager::FakeTokenManager(rng::Random* random)
    : firebase_id_token_(""),
      firebase_local_id_(convert::ToHex(random->RandomUniqueBytes())),
      email_("dummy@example.com") {}

void FakeTokenManager::Authorize(AppConfig app_config,
                                 fidl::InterfaceHandle<AuthenticationUIContext> auth_ui_context,
                                 std::vector<std::string> /*app_scopes*/,
                                 fidl::StringPtr /*user_profile_id*/, fidl::StringPtr /*auth_code*/,
                                 AuthorizeCallback callback /*callback*/) {
  FXL_NOTIMPLEMENTED() << "FakeTokenManager::Authorize not implemented";
  callback(fuchsia::auth::Status::INTERNAL_ERROR, nullptr);
}

void FakeTokenManager::GetAccessToken(AppConfig app_config, std::string /*user_profile_id*/,
                                      std::vector<std::string> /*app_scopes*/,
                                      GetAccessTokenCallback callback /*callback*/) {
  FXL_NOTIMPLEMENTED() << "FakeTokenManager::GetAccessToken not implemented";
  callback(fuchsia::auth::Status::INTERNAL_ERROR, nullptr);
}

void FakeTokenManager::GetIdToken(AppConfig app_config, std::string /*user_profile_id*/,
                                  fidl::StringPtr /*audience*/,
                                  GetIdTokenCallback callback /*callback*/) {
  FXL_NOTIMPLEMENTED() << "FakeTokenManager::GetIdToken not implemented";
  callback(fuchsia::auth::Status::INTERNAL_ERROR, {});
}

void FakeTokenManager::GetFirebaseToken(AppConfig /*app_config*/, std::string /*user_profile_id*/,
                                        std::string /*audience*/, std::string /*firebase_api_key*/,
                                        GetFirebaseTokenCallback callback) {
  if (firebase_local_id_.empty()) {
    callback(fuchsia::auth::Status::OK, nullptr);
    return;
  }
  fuchsia::auth::FirebaseTokenPtr token = fuchsia::auth::FirebaseToken::New();
  token->id_token = firebase_id_token_;
  token->local_id = firebase_local_id_;
  token->email = email_;
  callback(fuchsia::auth::Status::OK, std::move(token));
}

void FakeTokenManager::DeleteAllTokens(AppConfig /*app_config*/, std::string /*user_profile_id*/,
                                       bool /*force*/, DeleteAllTokensCallback callback) {
  FXL_NOTIMPLEMENTED() << "FakeTokenManager::DeleteAllTokens not implemented";
  callback(fuchsia::auth::Status::INTERNAL_ERROR);
}

void FakeTokenManager::ListProfileIds(AppConfig app_config, ListProfileIdsCallback callback) {
  FXL_NOTIMPLEMENTED() << "FakeTokenManager::ListProifleIds not implemented";
  callback(fuchsia::auth::Status::INTERNAL_ERROR, {});
}

}  // namespace firebase_auth

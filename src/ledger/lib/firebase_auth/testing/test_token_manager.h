// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_TEST_TOKEN_MANAGER_H_
#define SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_TEST_TOKEN_MANAGER_H_

#include <string>

#include <fuchsia/auth/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fxl/memory/ref_ptr.h>

using fuchsia::auth::AppConfig;
using fuchsia::auth::AuthenticationUIContext;

namespace firebase_auth {

class TestTokenManager : public fuchsia::auth::TokenManager {
 public:
  explicit TestTokenManager(async_dispatcher_t* dispatcher);

  ~TestTokenManager() override;

  // fuchsia::auth::TokenManager:
  void Authorize(AppConfig app_config,
                 fidl::InterfaceHandle<AuthenticationUIContext> auth_ui_context,
                 std::vector<std::string> app_scopes,
                 fidl::StringPtr user_profile_id, fidl::StringPtr auth_code,
                 AuthorizeCallback callback) override;

  void GetAccessToken(AppConfig app_config, std::string user_profile_id,
                      std::vector<std::string> app_scopes,
                      GetAccessTokenCallback callback) override;

  void GetIdToken(AppConfig app_config, std::string user_profile_id,
                  fidl::StringPtr audience,
                  GetIdTokenCallback callback) override;

  void GetFirebaseToken(AppConfig app_config, std::string user_profile_id,
                        std::string audience, std::string firebase_api_key,
                        GetFirebaseTokenCallback callback) override;

  void DeleteAllTokens(AppConfig app_config, std::string user_profile_id,
                       bool force, DeleteAllTokensCallback callback) override;

  void ListProfileIds(AppConfig app_config,
                      ListProfileIdsCallback callback) override;

  // Sets the token to return with the provided parameters, and status to OK.
  void Set(std::string id_token, std::string local_id, std::string email);

  // Sets the token to return to null, and status to |status|.
  // |status| must not be OK.
  void SetError(fuchsia::auth::Status status);

 private:
  async_dispatcher_t* const dispatcher_;
  fuchsia::auth::FirebaseTokenPtr token_to_return_;
  fuchsia::auth::Status status_to_return_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestTokenManager);
};

}  // namespace firebase_auth

#endif  // SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_TEST_TOKEN_MANAGER_H_

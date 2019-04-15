// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_FAKE_TOKEN_MANAGER_H_
#define SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_FAKE_TOKEN_MANAGER_H_

#include <fuchsia/auth/cpp/fidl.h>

#include <functional>

#include "peridot/lib/rng/random.h"
#include "src/lib/fxl/macros.h"

namespace firebase_auth {

using fuchsia::auth::AppConfig;
using fuchsia::auth::AuthenticationUIContext;

// FakeTokenManager is a dummy implementation of a TokenManager intended to be
// used to connect to unauthenticated firebase instances.
//
// The local ID Firebase token are set to a random UUID fixed at the
// construction time.
//
// Other token values are set to dummy const values.
class FakeTokenManager : public fuchsia::auth::TokenManager {
 public:
  FakeTokenManager(rng::Random* random);
  ~FakeTokenManager() override {}

 private:
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

  // Sets the token to return to null, and status to |status|.
  std::string firebase_id_token_;
  std::string firebase_local_id_;
  std::string email_;
  FXL_DISALLOW_COPY_AND_ASSIGN(FakeTokenManager);
};

}  // namespace firebase_auth

#endif  // SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_FAKE_TOKEN_MANAGER_H_

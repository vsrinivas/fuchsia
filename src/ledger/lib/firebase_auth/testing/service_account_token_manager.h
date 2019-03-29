// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_SERVICE_ACCOUNT_TOKEN_MANAGER_H_
#define SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_SERVICE_ACCOUNT_TOKEN_MANAGER_H_

#include <map>

#include <fuchsia/auth/cpp/fidl.h>
#include <src/lib/fxl/macros.h>

#include "src/ledger/lib/firebase_auth/testing/service_account_token_minter.h"

namespace service_account {

using fuchsia::auth::AppConfig;
using fuchsia::auth::AuthenticationUIContext;

// An implementation of |TokenManager| that uses a Firebase service account to
// register a new user of the given id and mint tokens for it.
//
// A Firebase service account with admin access to the project is automatically
// created for every Firebase project.
//
// In order to download the JSON credential file corresponding to this account,
// visit `Settings > Service accounts > Firebase admin SDK` in the Firebase
// Console and click on the 'Generate new private key' button. This JSON file
// must be available on the device, and its path must be passed to the
// LoadCredentials() method to initialize this class.
class ServiceAccountTokenManager : public fuchsia::auth::TokenManager {
 public:
  ServiceAccountTokenManager(network_wrapper::NetworkWrapper* network_wrapper,
                             std::unique_ptr<Credentials> credentials,
                             std::string user_id);
  ~ServiceAccountTokenManager() override;

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

 private:
  ServiceAccountTokenMinter service_account_token_minter_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ServiceAccountTokenManager);
};

};  // namespace service_account

#endif  // SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_SERVICE_ACCOUNT_TOKEN_MANAGER_H_

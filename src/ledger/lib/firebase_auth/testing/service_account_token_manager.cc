// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/lib/firebase_auth/testing/service_account_token_manager.h"

#include <src/lib/fxl/logging.h>

namespace service_account {
namespace {

fuchsia::auth::Status ConvertStatus(ServiceAccountTokenMinter::Status status) {
  switch (status) {
    case ServiceAccountTokenMinter::Status::OK:
      return fuchsia::auth::Status::OK;
    case ServiceAccountTokenMinter::Status::AUTH_SERVER_ERROR:
      return fuchsia::auth::Status::AUTH_PROVIDER_SERVER_ERROR;
    case ServiceAccountTokenMinter::Status::BAD_RESPONSE:
      return fuchsia::auth::Status::AUTH_PROVIDER_SERVER_ERROR;
    case ServiceAccountTokenMinter::Status::NETWORK_ERROR:
      return fuchsia::auth::Status::NETWORK_ERROR;
    case ServiceAccountTokenMinter::Status::INTERNAL_ERROR:
    default:
      return fuchsia::auth::Status::INTERNAL_ERROR;
  }
}

}  // namespace

ServiceAccountTokenManager::ServiceAccountTokenManager(
    network_wrapper::NetworkWrapper* network_wrapper,
    std::unique_ptr<Credentials> credentials, std::string user_id)
    : service_account_token_minter_(network_wrapper, std::move(credentials),
                                    std::move(user_id)) {}

ServiceAccountTokenManager::~ServiceAccountTokenManager() {}

void ServiceAccountTokenManager::Authorize(
    AppConfig app_config,
    fidl::InterfaceHandle<AuthenticationUIContext> auth_ui_context,
    std::vector<std::string> /*app_scopes*/,
    fidl::StringPtr /*user_profile_id*/, fidl::StringPtr /*auth_code*/,
    AuthorizeCallback callback /*callback*/) {
  FXL_NOTIMPLEMENTED();
  callback(fuchsia::auth::Status::INTERNAL_ERROR /*Not implemented*/, nullptr);
}

void ServiceAccountTokenManager::GetAccessToken(
    AppConfig app_config, std::string /*user_profile_id*/,
    std::vector<std::string> /*app_scopes*/, GetAccessTokenCallback callback) {
  FXL_NOTIMPLEMENTED();
  callback(fuchsia::auth::Status::INTERNAL_ERROR /*Not implemented*/, nullptr);
}

void ServiceAccountTokenManager::GetIdToken(
    AppConfig app_config, std::string /*user_profile_id*/,
    fidl::StringPtr /*audience*/, GetIdTokenCallback callback /*callback*/) {
  FXL_NOTIMPLEMENTED();
  callback(fuchsia::auth::Status::INTERNAL_ERROR /*Not implemented*/, nullptr);
}

void ServiceAccountTokenManager::GetFirebaseToken(
    AppConfig /*app_config*/, std::string /*user_profile_id*/,
    std::string /*audience*/, std::string firebase_api_key,
    GetFirebaseTokenCallback callback) {
  service_account_token_minter_.GetFirebaseToken(
      std::move(firebase_api_key),
      [this, callback = std::move(callback)](
          const ServiceAccountTokenMinter::GetTokenResponse& response) {
        auto status = ConvertStatus(response.status);

        if (response.status == ServiceAccountTokenMinter::Status::OK) {
          auto fb_token = fuchsia::auth::FirebaseToken::New();
          fb_token->id_token = response.id_token;
          fb_token->local_id = response.local_id;
          fb_token->email = response.email;
          callback(status, std::move(fb_token));
        } else {
          FXL_LOG(ERROR) << "Encountered error in GetToken(): "
                         << response.error_msg;
          callback(status, nullptr);
        }
      });
}

void ServiceAccountTokenManager::DeleteAllTokens(
    AppConfig app_config, std::string /*user_profile_id*/, bool /*force*/,
    DeleteAllTokensCallback callback /*callback*/) {
  FXL_NOTIMPLEMENTED();
  callback(fuchsia::auth::Status::INTERNAL_ERROR /*Not implemented*/);
}

void ServiceAccountTokenManager::ListProfileIds(
    AppConfig app_config, ListProfileIdsCallback callback /*callback*/) {
  FXL_NOTIMPLEMENTED();
  callback(fuchsia::auth::Status::INTERNAL_ERROR /*Not implemented*/, {});
}

}  // namespace service_account

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/firebase_auth/testing/service_account_token_provider.h"

#include <lib/fxl/logging.h>

namespace service_account {

namespace {

fuchsia::modular::auth::Status ConvertStatus(
    ServiceAccountTokenMinter::Status status) {
  switch (status) {
    case ServiceAccountTokenMinter::Status::OK:
      return fuchsia::modular::auth::Status::OK;
    case ServiceAccountTokenMinter::Status::AUTH_SERVER_ERROR:
      return fuchsia::modular::auth::Status::OAUTH_SERVER_ERROR;
    case ServiceAccountTokenMinter::Status::BAD_RESPONSE:
      return fuchsia::modular::auth::Status::BAD_RESPONSE;
    case ServiceAccountTokenMinter::Status::NETWORK_ERROR:
      return fuchsia::modular::auth::Status::NETWORK_ERROR;
    case ServiceAccountTokenMinter::Status::INTERNAL_ERROR:
    default:
      return fuchsia::modular::auth::Status::INTERNAL_ERROR;
  }
}

fuchsia::modular::auth::AuthErr GetError(fuchsia::modular::auth::Status status,
                                         std::string message) {
  fuchsia::modular::auth::AuthErr error;
  error.status = status;
  error.message = message;
  return error;
}

}  // namespace

ServiceAccountTokenProvider::ServiceAccountTokenProvider(
    network_wrapper::NetworkWrapper* network_wrapper,
    std::unique_ptr<Credentials> credentials, std::string user_id)
    : service_account_token_minter_(network_wrapper, std::move(credentials),
                                    std::move(user_id)) {}

ServiceAccountTokenProvider::~ServiceAccountTokenProvider() {}

void ServiceAccountTokenProvider::GetAccessToken(
    GetAccessTokenCallback callback) {
  FXL_NOTIMPLEMENTED();
  callback(nullptr, GetError(fuchsia::modular::auth::Status::INTERNAL_ERROR,
                             "Not implemented."));
}

void ServiceAccountTokenProvider::GetIdToken(GetIdTokenCallback callback) {
  FXL_NOTIMPLEMENTED();
  callback(nullptr, GetError(fuchsia::modular::auth::Status::INTERNAL_ERROR,
                             "Not implemented."));
}

void ServiceAccountTokenProvider::GetFirebaseAuthToken(
    fidl::StringPtr firebase_api_key, GetFirebaseAuthTokenCallback callback) {
  service_account_token_minter_.GetFirebaseToken(
      std::move(firebase_api_key),
      [this, callback = std::move(callback)](
          const ServiceAccountTokenMinter::GetTokenResponse& response) {
        auto error =
            GetError(ConvertStatus(response.status), response.error_msg);

        if (response.status == ServiceAccountTokenMinter::Status::OK) {
          auto fb_token = fuchsia::modular::auth::FirebaseToken::New();
          fb_token->id_token = response.id_token;
          fb_token->local_id = response.local_id;
          fb_token->email = response.email;
          callback(std::move(fb_token), std::move(error));
        } else {
          callback(nullptr, std::move(error));
        }
      });
}

void ServiceAccountTokenProvider::GetClientId(GetClientIdCallback callback) {
  callback(service_account_token_minter_.GetClientId());
}

}  // namespace service_account

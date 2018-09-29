// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIREBASE_AUTH_TESTING_SERVICE_ACCOUNT_TOKEN_PROVIDER_H_
#define PERIDOT_LIB_FIREBASE_AUTH_TESTING_SERVICE_ACCOUNT_TOKEN_PROVIDER_H_

#include <map>

#include <fuchsia/modular/auth/cpp/fidl.h>
#include <lib/fxl/macros.h>

#include "peridot/lib/firebase_auth/testing/service_account_token_minter.h"

namespace service_account {

// An implementation of |TokenProvider| that uses a Firebase service account to
// register a new user of the given id and mint tokens for it.
class ServiceAccountTokenProvider
    : public fuchsia::modular::auth::TokenProvider {
 public:
  ServiceAccountTokenProvider(network_wrapper::NetworkWrapper* network_wrapper,
                              std::unique_ptr<Credentials> credentials,
                              std::string user_id);
  ~ServiceAccountTokenProvider() override;

  // fuchsia::modular::auth::TokenProvider:
  void GetAccessToken(GetAccessTokenCallback callback) override;
  void GetIdToken(GetIdTokenCallback callback) override;
  void GetFirebaseAuthToken(fidl::StringPtr firebase_api_key,
                            GetFirebaseAuthTokenCallback callback) override;
  void GetClientId(GetClientIdCallback callback) override;

 private:
  ServiceAccountTokenMinter service_account_token_minter_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ServiceAccountTokenProvider);
};

};  // namespace service_account

#endif  // PERIDOT_LIB_FIREBASE_AUTH_TESTING_SERVICE_ACCOUNT_TOKEN_PROVIDER_H_

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_TEST_TOKEN_PROVIDER_SERVICE_ACCOUNT_TOKEN_PROVIDER_H_
#define PERIDOT_BIN_LEDGER_TEST_TOKEN_PROVIDER_SERVICE_ACCOUNT_TOKEN_PROVIDER_H_

#include <map>

#include "lib/auth/fidl/token_provider.fidl.h"
#include "peridot/bin/ledger/callback/cancellable.h"
#include "peridot/bin/ledger/network/network_service.h"

#include "lib/fxl/macros.h"

namespace test {

// An implementation of |TokenProvider| that uses a firebase service account to
// register an user and mint token for it.
class ServiceAccountTokenProvider : public modular::auth::TokenProvider {
 public:
  ServiceAccountTokenProvider(ledger::NetworkService* network_service,
                              std::string user_id);
  ~ServiceAccountTokenProvider() override;

  // Load the service account credentials. This method must be called before
  // this class is usable. |json_file| must be a path to the service account
  // configuration file that can be retrieved from the firebase admin console.
  bool LoadCredentials(const std::string& json_file);

  // modular::auth::TokenProvider
  void GetAccessToken(const GetAccessTokenCallback& callback) override;
  void GetIdToken(const GetIdTokenCallback& callback) override;
  void GetFirebaseAuthToken(
      const fidl::String& firebase_api_key,
      const GetFirebaseAuthTokenCallback& callback) override;
  void GetClientId(const GetClientIdCallback& callback) override;

 private:
  struct Credentials;
  struct CachedToken;

  std::string GetClaims();
  bool GetCustomToken(std::string* custom_token);
  modular::auth::FirebaseTokenPtr GetFirebaseToken(const std::string& id_token);
  network::URLRequestPtr GetIdentityRequest(const std::string& api_key,
                                            const std::string& custom_token);
  std::string GetIdentityRequestBody(const std::string& custom_token);
  void HandleIdentityResponse(const std::string& api_key,
                              network::URLResponsePtr response);
  void ResolveCallbacks(const std::string& api_key,
                        modular::auth::FirebaseTokenPtr token,
                        modular::auth::AuthErrPtr error);

  ledger::NetworkService* network_service_;
  const std::string user_id_;
  std::unique_ptr<Credentials> credentials_;
  std::map<std::string, std::unique_ptr<CachedToken>> cached_tokens_;
  std::map<std::string, std::vector<GetFirebaseAuthTokenCallback>>
      in_progress_callbacks_;
  callback::CancellableContainer in_progress_requests_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ServiceAccountTokenProvider);
};

};  // namespace test

#endif  // PERIDOT_BIN_LEDGER_TEST_TOKEN_PROVIDER_SERVICE_ACCOUNT_TOKEN_PROVIDER_H_

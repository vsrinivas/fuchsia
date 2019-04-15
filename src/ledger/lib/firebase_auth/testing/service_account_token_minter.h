// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_SERVICE_ACCOUNT_TOKEN_MINTER_H_
#define SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_SERVICE_ACCOUNT_TOKEN_MINTER_H_

#include <lib/callback/cancellable.h>
#include <lib/fit/function.h>
#include <lib/network_wrapper/network_wrapper.h>

#include <map>

#include "src/ledger/lib/firebase_auth/testing/credentials.h"
#include "src/lib/fxl/macros.h"

namespace service_account {

// An implementation of |fuchsia::auth::TokenManager| that uses a Firebase
// service account to register a new user of the given id and mint tokens for
// it.
class ServiceAccountTokenMinter {
 public:
  enum Status {
    OK,
    NETWORK_ERROR,
    BAD_RESPONSE,
    AUTH_SERVER_ERROR,
    INTERNAL_ERROR
  };

  struct GetTokenResponse {
    Status status;
    std::string id_token;
    std::string local_id;
    std::string email;
    std::string error_msg;
  };
  ServiceAccountTokenMinter(network_wrapper::NetworkWrapper* network_wrapper,
                            std::unique_ptr<Credentials> credentials,
                            std::string user_id);

  ~ServiceAccountTokenMinter();

  using GetFirebaseTokenCallback =
      fit::function<void(const GetTokenResponse& response)>;

  void GetFirebaseToken(fidl::StringPtr firebase_api_key,
                        GetFirebaseTokenCallback callback);

  std::string GetClientId();

 private:
  struct CachedToken;

  std::string GetClaims();
  bool GetCustomToken(std::string* custom_token);
  GetTokenResponse GetCachedToken(fidl::StringPtr firebase_api_key);
  GetTokenResponse GetSuccessResponse(const std::string& id_token);
  GetTokenResponse GetErrorResponse(Status status,
                                    const std::string& error_msg);
  ::fuchsia::net::oldhttp::URLRequest GetIdentityRequest(
      const std::string& api_key, const std::string& custom_token);
  std::string GetIdentityRequestBody(const std::string& custom_token);
  void HandleIdentityResponse(const std::string& api_key,
                              ::fuchsia::net::oldhttp::URLResponse response);
  void ResolveCallbacks(const std::string& api_key, GetTokenResponse response);

  network_wrapper::NetworkWrapper* network_wrapper_;
  std::unique_ptr<Credentials> credentials_;
  const std::string user_id_;
  std::map<std::string, std::unique_ptr<CachedToken>> cached_tokens_;
  std::map<std::string, std::vector<GetFirebaseTokenCallback>>
      in_progress_callbacks_;
  callback::CancellableContainer in_progress_requests_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ServiceAccountTokenMinter);
};

};  // namespace service_account

#endif  // SRC_LEDGER_LIB_FIREBASE_AUTH_TESTING_SERVICE_ACCOUNT_TOKEN_MINTER_H_

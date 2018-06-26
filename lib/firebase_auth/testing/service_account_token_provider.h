// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIREBASE_AUTH_TESTING_SERVICE_ACCOUNT_TOKEN_PROVIDER_H_
#define PERIDOT_LIB_FIREBASE_AUTH_TESTING_SERVICE_ACCOUNT_TOKEN_PROVIDER_H_

#include <map>

#include <fuchsia/modular/auth/cpp/fidl.h>

#include "lib/callback/cancellable.h"
#include "lib/fxl/macros.h"
#include "lib/network_wrapper/network_wrapper.h"

namespace service_account {

// An implementation of |TokenProvider| that uses a Firebase service account to
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
class ServiceAccountTokenProvider : public fuchsia::modular::auth::TokenProvider {
 public:
  ServiceAccountTokenProvider(network_wrapper::NetworkWrapper* network_wrapper,
                              std::string user_id);
  ~ServiceAccountTokenProvider() override;

  // Loads the service account credentials.
  //
  // This method must be called before this class is usable. |json_file| must be
  // a path to the service account configuration file that can be retrieved from
  // the firebase admin console (see the class-level comment).
  bool LoadCredentials(const std::string& json_file);

  // fuchsia::modular::auth::TokenProvider:
  void GetAccessToken(GetAccessTokenCallback callback) override;
  void GetIdToken(GetIdTokenCallback callback) override;
  void GetFirebaseAuthToken(fidl::StringPtr firebase_api_key,
                            GetFirebaseAuthTokenCallback callback) override;
  void GetClientId(GetClientIdCallback callback) override;

 private:
  struct Credentials;
  struct CachedToken;

  std::string GetClaims();
  bool GetCustomToken(std::string* custom_token);
  fuchsia::modular::auth::FirebaseTokenPtr GetFirebaseToken(const std::string& id_token);
  ::fuchsia::net::oldhttp::URLRequest GetIdentityRequest(
      const std::string& api_key, const std::string& custom_token);
  std::string GetIdentityRequestBody(const std::string& custom_token);
  void HandleIdentityResponse(const std::string& api_key,
                              ::fuchsia::net::oldhttp::URLResponse response);
  void ResolveCallbacks(const std::string& api_key,
                        fuchsia::modular::auth::FirebaseTokenPtr token,
                        fuchsia::modular::auth::AuthErr error);

  network_wrapper::NetworkWrapper* network_wrapper_;
  const std::string user_id_;
  std::unique_ptr<Credentials> credentials_;
  std::map<std::string, std::unique_ptr<CachedToken>> cached_tokens_;
  std::map<std::string, std::vector<GetFirebaseAuthTokenCallback>>
      in_progress_callbacks_;
  callback::CancellableContainer in_progress_requests_;

  FXL_DISALLOW_COPY_AND_ASSIGN(ServiceAccountTokenProvider);
};

};  // namespace service_account

#endif  // PERIDOT_LIB_FIREBASE_AUTH_TESTING_SERVICE_ACCOUNT_TOKEN_PROVIDER_H_

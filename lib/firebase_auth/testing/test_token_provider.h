// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIREBASE_AUTH_TESTING_TEST_TOKEN_PROVIDER_H_
#define PERIDOT_LIB_FIREBASE_AUTH_TESTING_TEST_TOKEN_PROVIDER_H_

#include <string>

#include <fuchsia/modular/auth/cpp/fidl.h>
#include <lib/async/dispatcher.h>
#include <lib/fxl/functional/make_copyable.h>
#include <lib/fxl/memory/ref_ptr.h>

namespace firebase_auth {

class TestTokenProvider : public fuchsia::modular::auth::TokenProvider {
 public:
  explicit TestTokenProvider(async_t* async);

  ~TestTokenProvider() override;

  // fuchsia::modular::auth::TokenProvider:
  void GetAccessToken(GetAccessTokenCallback callback) override;

  void GetIdToken(GetIdTokenCallback callback) override;

  void GetFirebaseAuthToken(fidl::StringPtr firebase_api_key,
                            GetFirebaseAuthTokenCallback callback) override;

  void GetClientId(GetClientIdCallback /*callback*/) override;

  // Sets the token to return with the provided parameters, and status to OK.
  void Set(std::string id_token, std::string local_id, std::string email);

  // Sets the token to return to null, and status to |error|.
  // |error| must not be OK.
  void SetError(fuchsia::modular::auth::Status error);

 private:
  async_t* const async_;
  fuchsia::modular::auth::FirebaseTokenPtr token_to_return_;
  fuchsia::modular::auth::AuthErr error_to_return_;

  FXL_DISALLOW_COPY_AND_ASSIGN(TestTokenProvider);
};

}  // namespace firebase_auth

#endif  // PERIDOT_LIB_FIREBASE_AUTH_TESTING_TEST_TOKEN_PROVIDER_H_

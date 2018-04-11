// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIREBASE_AUTH_TESTING_TEST_TOKEN_PROVIDER_H_
#define PERIDOT_LIB_FIREBASE_AUTH_TESTING_TEST_TOKEN_PROVIDER_H_

#include <string>

#include <fuchsia/cpp/modular_auth.h>
#include <lib/async/dispatcher.h>

#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/memory/ref_ptr.h"

namespace firebase_auth {

class TestTokenProvider : public modular_auth::TokenProvider {
 public:
  explicit TestTokenProvider(async_t* async);

  ~TestTokenProvider() override;

  // modular_auth::TokenProvider:
  void GetAccessToken(GetAccessTokenCallback callback) override;

  void GetIdToken(GetIdTokenCallback callback) override;

  void GetFirebaseAuthToken(fidl::StringPtr firebase_api_key,
                            GetFirebaseAuthTokenCallback callback) override;

  void GetClientId(GetClientIdCallback /*callback*/) override;

  void Set(std::string id_token, std::string local_id, std::string email);

  void SetNull();

  modular_auth::FirebaseTokenPtr token_to_return;
  modular_auth::AuthErr error_to_return;

 private:
  async_t* const async_;
  FXL_DISALLOW_COPY_AND_ASSIGN(TestTokenProvider);
};

}  // namespace firebase_auth

#endif  // PERIDOT_LIB_FIREBASE_AUTH_TESTING_TEST_TOKEN_PROVIDER_H_

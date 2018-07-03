// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIREBASE_AUTH_TESTING_FAKE_TOKEN_PROVIDER_H_
#define PERIDOT_LIB_FIREBASE_AUTH_TESTING_FAKE_TOKEN_PROVIDER_H_

#include <functional>

#include <fuchsia/modular/auth/cpp/fidl.h>
#include <lib/fxl/macros.h>

namespace firebase_auth {
// FakeTokenProvider is a dummy implementation of a TokenProvider intended to be
// used to connect to unauthenticated firebase instances.
//
// The local ID Firebase token are set to a random UUID fixed at the
// construction time.
//
// Other token values are set to dummy const values.
class FakeTokenProvider : public fuchsia::modular::auth::TokenProvider {
 public:
  FakeTokenProvider();
  ~FakeTokenProvider() override {}

 private:
  void GetAccessToken(GetAccessTokenCallback callback) override;
  void GetIdToken(GetIdTokenCallback callback) override;
  void GetFirebaseAuthToken(fidl::StringPtr firebase_api_key,
                            GetFirebaseAuthTokenCallback callback) override;
  void GetClientId(GetClientIdCallback callback) override;

  std::string firebase_id_token_;
  std::string firebase_local_id_;
  std::string email_;
  std::string client_id_;
  FXL_DISALLOW_COPY_AND_ASSIGN(FakeTokenProvider);
};

}  // namespace firebase_auth

#endif  // PERIDOT_LIB_FIREBASE_AUTH_TESTING_FAKE_TOKEN_PROVIDER_H_

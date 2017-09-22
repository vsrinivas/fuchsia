// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_TEST_FAKE_TOKEN_PROVIDER_H_
#define APPS_LEDGER_SRC_TEST_FAKE_TOKEN_PROVIDER_H_

#include <functional>

#include "lib/auth/fidl/token_provider.fidl.h"

namespace test {
// FakeTokenProvider is a dummy implementation of a TokenProvider intended to be
// used to connect to unauthenticated firebase instances.
class FakeTokenProvider : public modular::auth::TokenProvider {
 public:
  FakeTokenProvider(std::string firebase_id_token,
                    std::string firebase_local_id,
                    std::string email,
                    std::string client_id);
  ~FakeTokenProvider() override {}

 private:
  void GetAccessToken(const GetAccessTokenCallback& callback) override;
  void GetIdToken(const GetIdTokenCallback& callback) override;
  void GetFirebaseAuthToken(
      const fidl::String& firebase_api_key,
      const GetFirebaseAuthTokenCallback& callback) override;
  void GetClientId(const GetClientIdCallback& callback) override;

  std::string firebase_id_token_;
  std::string firebase_local_id_;
  std::string email_;
  std::string client_id_;
  FXL_DISALLOW_COPY_AND_ASSIGN(FakeTokenProvider);
};

}  // namespace test

#endif  // APPS_LEDGER_SRC_TEST_FAKE_TOKEN_PROVIDER_H_

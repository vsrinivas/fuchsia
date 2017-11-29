// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_LIB_FIREBASE_AUTH_TEST_TEST_TOKEN_PROVIDER_H_
#define PERIDOT_LIB_FIREBASE_AUTH_TEST_TEST_TOKEN_PROVIDER_H_

#include <string>

#include "lib/auth/fidl/token_provider.fidl.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/memory/ref_ptr.h"
#include "lib/fxl/tasks/task_runner.h"

namespace firebase_auth {

namespace test {
class TestTokenProvider : public modular::auth::TokenProvider {
 public:
  explicit TestTokenProvider(fxl::RefPtr<fxl::TaskRunner> task_runner);

  ~TestTokenProvider() override;

  // modular::auth::TokenProvider:
  void GetAccessToken(const GetAccessTokenCallback& callback) override;

  void GetIdToken(const GetIdTokenCallback& callback) override;

  void GetFirebaseAuthToken(
      const fidl::String& firebase_api_key,
      const GetFirebaseAuthTokenCallback& callback) override;

  void GetClientId(const GetClientIdCallback& /*callback*/) override;

  void Set(std::string id_token, std::string local_id, std::string email);

  void SetNull();

  modular::auth::FirebaseTokenPtr token_to_return;
  modular::auth::AuthErrPtr error_to_return;

 private:
  fxl::RefPtr<fxl::TaskRunner> task_runner_;
  FXL_DISALLOW_COPY_AND_ASSIGN(TestTokenProvider);
};

}  // namespace test
}  // namespace firebase_auth

#endif  // PERIDOT_LIB_FIREBASE_AUTH_TEST_TEST_TOKEN_PROVIDER_H_

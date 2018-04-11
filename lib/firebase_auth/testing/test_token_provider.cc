// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/firebase_auth/testing/test_token_provider.h"

#include <lib/async/cpp/task.h>

#include "lib/fidl/cpp/clone.h"
#include "lib/fidl/cpp/optional.h"

namespace firebase_auth {
TestTokenProvider::TestTokenProvider(async_t* async) : async_(async) {
  error_to_return.status = modular_auth::Status::OK;
  error_to_return.message = "";
}

TestTokenProvider::~TestTokenProvider() {}

void TestTokenProvider::GetAccessToken(GetAccessTokenCallback /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

void TestTokenProvider::GetIdToken(GetIdTokenCallback /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

void TestTokenProvider::GetFirebaseAuthToken(
    fidl::StringPtr /*firebase_api_key*/,
    GetFirebaseAuthTokenCallback callback) {
  modular_auth::FirebaseTokenPtr token_to_return_copy;
  fidl::Clone(token_to_return, &token_to_return_copy);
  modular_auth::AuthErr error_to_return_copy;
  fidl::Clone(error_to_return, &error_to_return_copy);
  async::PostTask(async_, fxl::MakeCopyable(
      [token_to_return = std::move(token_to_return_copy),
       error_to_return = std::move(error_to_return_copy), callback]() mutable {
        callback(std::move(token_to_return), std::move(error_to_return));
      }));
}

void TestTokenProvider::GetClientId(GetClientIdCallback /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

void TestTokenProvider::Set(std::string id_token,
                            std::string local_id,
                            std::string email) {
  token_to_return = modular_auth::FirebaseToken::New();
  token_to_return->id_token = id_token;
  token_to_return->local_id = local_id;
  token_to_return->email = email;
}

void TestTokenProvider::SetNull() {
  token_to_return = nullptr;
}
}  // namespace firebase_auth

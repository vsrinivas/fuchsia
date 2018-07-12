// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/firebase_auth/testing/test_token_provider.h"

#include <lib/async/cpp/task.h>
#include <lib/fidl/cpp/clone.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fxl/logging.h>

namespace firebase_auth {
TestTokenProvider::TestTokenProvider(async_dispatcher_t* dispatcher) : dispatcher_(dispatcher) {
  error_to_return_.status = fuchsia::modular::auth::Status::OK;
  error_to_return_.message = "";
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
  fuchsia::modular::auth::FirebaseTokenPtr token_to_return_copy;
  fidl::Clone(token_to_return_, &token_to_return_copy);
  fuchsia::modular::auth::AuthErr error_to_return_copy;
  fidl::Clone(error_to_return_, &error_to_return_copy);
  async::PostTask(dispatcher_, [token_to_return = std::move(token_to_return_copy),
                           error_to_return = std::move(error_to_return_copy),
                           callback = std::move(callback)]() mutable {
    callback(std::move(token_to_return), std::move(error_to_return));
  });
}

void TestTokenProvider::GetClientId(GetClientIdCallback /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

void TestTokenProvider::Set(std::string id_token, std::string local_id,
                            std::string email) {
  token_to_return_ = fuchsia::modular::auth::FirebaseToken::New();
  token_to_return_->id_token = id_token;
  token_to_return_->local_id = local_id;
  token_to_return_->email = email;
  error_to_return_.status = fuchsia::modular::auth::Status::OK;
  error_to_return_.message = "";
}

void TestTokenProvider::SetError(fuchsia::modular::auth::Status error) {
  FXL_CHECK(error != fuchsia::modular::auth::Status::OK);
  token_to_return_ = nullptr;
  error_to_return_.status = error;
}
}  // namespace firebase_auth

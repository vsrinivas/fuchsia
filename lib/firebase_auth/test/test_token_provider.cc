// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/firebase_auth/test/test_token_provider.h"

namespace firebase_auth {
namespace test {
TestTokenProvider::TestTokenProvider(fxl::RefPtr<fxl::TaskRunner> task_runner)
    : task_runner_(std::move(task_runner)) {
  error_to_return = modular::auth::AuthErr::New();
  error_to_return->status = modular::auth::Status::OK;
  error_to_return->message = "";
}

TestTokenProvider::~TestTokenProvider() {}

void TestTokenProvider::GetAccessToken(
    const GetAccessTokenCallback& /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

void TestTokenProvider::GetIdToken(const GetIdTokenCallback& /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

void TestTokenProvider::GetFirebaseAuthToken(
    const fidl::String& /*firebase_api_key*/,
    const GetFirebaseAuthTokenCallback& callback) {
  task_runner_->PostTask(fxl::MakeCopyable(
      [token_to_return = token_to_return.Clone(),
       error_to_return = error_to_return.Clone(), callback]() mutable {
        callback(std::move(token_to_return), std::move(error_to_return));
      }));
}

void TestTokenProvider::GetClientId(const GetClientIdCallback& /*callback*/) {
  FXL_NOTIMPLEMENTED();
}

void TestTokenProvider::Set(std::string id_token,
                            std::string local_id,
                            std::string email) {
  token_to_return = modular::auth::FirebaseToken::New();
  token_to_return->id_token = id_token;
  token_to_return->local_id = local_id;
  token_to_return->email = email;
}

void TestTokenProvider::SetNull() {
  token_to_return = nullptr;
}
}  // namespace test
}  // namespace firebase_auth

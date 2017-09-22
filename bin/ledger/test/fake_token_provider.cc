// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/test/fake_token_provider.h"

namespace test {

FakeTokenProvider::FakeTokenProvider(std::string firebase_id_token,
                                     std::string firebase_local_id,
                                     std::string email,
                                     std::string client_id)
    : firebase_id_token_(std::move(firebase_id_token)),
      firebase_local_id_(std::move(firebase_local_id)),
      email_(std::move(email)),
      client_id_(std::move(client_id)) {}

void FakeTokenProvider::GetAccessToken(const GetAccessTokenCallback& callback) {
  modular::auth::AuthErrPtr error = modular::auth::AuthErr::New();
  error->status = modular::auth::Status::OK;
  error->message = "";
  FXL_NOTIMPLEMENTED() << "FakeTokenProvider::GetAccessToken not implemented";
  callback(nullptr, std::move(error));
}

void FakeTokenProvider::GetIdToken(const GetIdTokenCallback& callback) {
  modular::auth::AuthErrPtr error = modular::auth::AuthErr::New();
  error->status = modular::auth::Status::OK;
  error->message = "";
  FXL_NOTIMPLEMENTED() << "FakeTokenProvider::GetIdToken not implemented";
  callback(nullptr, std::move(error));
}

void FakeTokenProvider::GetFirebaseAuthToken(
    const fidl::String& /*firebase_api_key*/,
    const GetFirebaseAuthTokenCallback& callback) {
  modular::auth::AuthErrPtr error = modular::auth::AuthErr::New();
  error->status = modular::auth::Status::OK;
  error->message = "";
  if (firebase_local_id_.empty()) {
    callback(nullptr, std::move(error));
    return;
  }
  modular::auth::FirebaseTokenPtr token = modular::auth::FirebaseToken::New();
  token->id_token = firebase_id_token_;
  token->local_id = firebase_local_id_;
  token->email = email_;
  callback(std::move(token), std::move(error));
}

void FakeTokenProvider::GetClientId(const GetClientIdCallback& callback) {
  if (client_id_.empty()) {
    callback(nullptr);
  } else {
    callback(client_id_);
  }
}

}  // namespace test

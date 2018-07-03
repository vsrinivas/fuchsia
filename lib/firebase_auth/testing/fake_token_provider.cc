// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/firebase_auth/testing/fake_token_provider.h"

#include <lib/fxl/logging.h>
#include <lib/fxl/random/uuid.h>

namespace firebase_auth {

FakeTokenProvider::FakeTokenProvider()
    : firebase_id_token_(""),
      firebase_local_id_(fxl::GenerateUUID()),
      email_("dummy@example.com"),
      client_id_("client_id") {}

void FakeTokenProvider::GetAccessToken(GetAccessTokenCallback callback) {
  fuchsia::modular::auth::AuthErr error;
  error.status = fuchsia::modular::auth::Status::OK;
  error.message = "";
  FXL_NOTIMPLEMENTED() << "FakeTokenProvider::GetAccessToken not implemented";
  callback(nullptr, std::move(error));
}

void FakeTokenProvider::GetIdToken(GetIdTokenCallback callback) {
  fuchsia::modular::auth::AuthErr error;
  error.status = fuchsia::modular::auth::Status::OK;
  error.message = "";
  FXL_NOTIMPLEMENTED() << "FakeTokenProvider::GetIdToken not implemented";
  callback(nullptr, std::move(error));
}

void FakeTokenProvider::GetFirebaseAuthToken(
    fidl::StringPtr /*firebase_api_key*/,
    GetFirebaseAuthTokenCallback callback) {
  fuchsia::modular::auth::AuthErr error;
  error.status = fuchsia::modular::auth::Status::OK;
  error.message = "";
  if (firebase_local_id_.empty()) {
    callback(nullptr, std::move(error));
    return;
  }
  fuchsia::modular::auth::FirebaseTokenPtr token =
      fuchsia::modular::auth::FirebaseToken::New();
  token->id_token = firebase_id_token_;
  token->local_id = firebase_local_id_;
  token->email = email_;
  callback(std::move(token), std::move(error));
}

void FakeTokenProvider::GetClientId(GetClientIdCallback callback) {
  if (client_id_.empty()) {
    callback(nullptr);
  } else {
    callback(client_id_);
  }
}

}  // namespace firebase_auth

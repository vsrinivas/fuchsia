// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/cloud_provider_firestore/app/credentials_provider_impl.h"

namespace cloud_provider_firestore {

namespace {

class FirebaseAuthPlugin : public grpc::MetadataCredentialsPlugin {
 public:
  explicit FirebaseAuthPlugin(std::string token)
      : header_value_("Bearer " + token) {}

  grpc::Status GetMetadata(
      grpc::string_ref /*service_url*/, grpc::string_ref /*method_name*/,
      const grpc::AuthContext& /*channel_auth_context*/,
      std::multimap<grpc::string, grpc::string>* metadata) override {
    // note: grpc seems to insist on lowercase "authorization", otherwise we get
    // "Illegal header key" from "src/core/lib/surface/validate_metadata.c".
    metadata->insert(std::make_pair("authorization", header_value_));
    return grpc::Status::OK;
  }

 private:
  const std::string header_value_;
};

std::shared_ptr<grpc::CallCredentials> MakeCredentials(std::string token) {
  return grpc::MetadataCredentialsFromPlugin(
      std::make_unique<FirebaseAuthPlugin>(std::move(token)));
}

}  // namespace

CredentialsProviderImpl::CredentialsProviderImpl(
    std::unique_ptr<firebase_auth::FirebaseAuth> firebase_auth)
    : firebase_auth_(std::move(firebase_auth)) {}

CredentialsProviderImpl::~CredentialsProviderImpl() {}

void CredentialsProviderImpl::GetCredentials(
    std::function<void(std::shared_ptr<grpc::CallCredentials>)> callback) {
  auto request = firebase_auth_->GetFirebaseToken(
      [this, callback = std::move(callback)](
          firebase_auth::AuthStatus auth_status, std::string auth_token) {
        switch (auth_status) {
          case firebase_auth::AuthStatus::OK:
            callback(MakeCredentials(auth_token));
            return;
          case firebase_auth::AuthStatus::ERROR:
            callback(nullptr);
            return;
        }
      });
  auth_token_requests_.emplace(request);
}

}  // namespace cloud_provider_firestore

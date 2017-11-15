// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/auth/token_manager/test/dev_auth_provider_impl.h"

namespace {
std::string GenerateRandomString() {
  uint32_t random_number;
  size_t random_size;
  zx_status_t status =
      zx_cprng_draw(&random_number, sizeof random_number, &random_size);
  FXL_CHECK(status == ZX_OK);
  FXL_CHECK(sizeof random_number == random_size);
  return std::to_string(random_number);
}

}  // namespace

namespace auth {
namespace dev_auth_provider {

using auth::dev_auth_provider::DevAuthProviderImpl;

DevAuthProviderImpl::DevAuthProviderImpl() {}

DevAuthProviderImpl::~DevAuthProviderImpl() {}

void DevAuthProviderImpl::GetPersistentCredential(
    fidl::InterfaceHandle<auth::AuthenticationUIContext> auth_ui_context,
    const GetPersistentCredentialCallback& callback) {
  callback(AuthProviderStatus::OK, GenerateRandomString());
  return;
}

void DevAuthProviderImpl::GetAppAccessToken(
    const fidl::String& credential, const fidl::String& app_client_id,
    const fidl::Array<fidl::String> app_scopes,
    const GetAppAccessTokenCallback& callback) {
  AuthTokenPtr access_token = auth::AuthToken::New();
  access_token->token =
      std::string(credential) + ":at_" + GenerateRandomString();
  access_token->token_type = TokenType::ACCESS_TOKEN;
  access_token->expires_in = 3600;

  callback(AuthProviderStatus::OK, std::move(access_token));
}

void DevAuthProviderImpl::GetAppIdToken(const fidl::String& credential,
                                        const fidl::String& audience,
                                        const GetAppIdTokenCallback& callback) {
  AuthTokenPtr id_token = auth::AuthToken::New();
  id_token->token = std::string(credential) + ":idt_" + GenerateRandomString();
  id_token->token_type = TokenType::ID_TOKEN;
  id_token->expires_in = 3600;

  callback(AuthProviderStatus::OK, std::move(id_token));
}

void DevAuthProviderImpl::GetAppFirebaseToken(
    const fidl::String& id_token, const fidl::String& firebase_api_key,
    const GetAppFirebaseTokenCallback& callback) {
  callback(AuthProviderStatus::OK, nullptr);
}

void DevAuthProviderImpl::RevokeAppOrPersistentCredential(
    const fidl::String& credential,
    const RevokeAppOrPersistentCredentialCallback& callback) {
  callback(AuthProviderStatus::OK);
}

}  // namespace dev_auth_provider
}  // namespace auth

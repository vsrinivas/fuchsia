// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/auth/token_manager/test/dev_auth_provider_impl.h"

namespace {

std::string GenerateRandomString() {
  uint32_t random_number;
  zx_cprng_draw(&random_number, sizeof random_number);
  return std::to_string(random_number);
}

}  // namespace

namespace auth {
namespace dev_auth_provider {

using auth::dev_auth_provider::DevAuthProviderImpl;
using fuchsia::auth::AuthenticationUIContext;
using fuchsia::auth::AuthProviderStatus;
using fuchsia::auth::AuthTokenPtr;
using fuchsia::auth::FirebaseTokenPtr;

DevAuthProviderImpl::DevAuthProviderImpl() {}

DevAuthProviderImpl::~DevAuthProviderImpl() {}

void DevAuthProviderImpl::GetPersistentCredential(
    fidl::InterfaceHandle<AuthenticationUIContext> auth_ui_context,
    GetPersistentCredentialCallback callback) {
  fuchsia::auth::UserProfileInfoPtr ui = fuchsia::auth::UserProfileInfo::New();
  ui->id = GenerateRandomString() + "@example.com";
  ui->display_name = "test_user_display_name";
  ui->url = "http://test_user/profile/url";
  ui->image_url = "http://test_user/profile/image/url";

  callback(AuthProviderStatus::OK, "rt_" + GenerateRandomString(),
           std::move(ui));
}

void DevAuthProviderImpl::GetAppAccessToken(
    fidl::StringPtr credential,
    fidl::StringPtr app_client_id,
    const fidl::VectorPtr<fidl::StringPtr> app_scopes,
    GetAppAccessTokenCallback callback) {
  AuthTokenPtr access_token = fuchsia::auth::AuthToken::New();
  access_token->token =
      std::string(credential) + ":at_" + GenerateRandomString();
  access_token->token_type = fuchsia::auth::TokenType::ACCESS_TOKEN;
  access_token->expires_in = 3600;

  callback(AuthProviderStatus::OK, std::move(access_token));
}

void DevAuthProviderImpl::GetAppIdToken(fidl::StringPtr credential,
                                        fidl::StringPtr audience,
                                        GetAppIdTokenCallback callback) {
  AuthTokenPtr id_token = fuchsia::auth::AuthToken::New();
  id_token->token = std::string(credential) + ":idt_" + GenerateRandomString();
  id_token->token_type = fuchsia::auth::TokenType::ID_TOKEN;
  id_token->expires_in = 3600;

  callback(AuthProviderStatus::OK, std::move(id_token));
}

void DevAuthProviderImpl::GetAppFirebaseToken(
    fidl::StringPtr id_token,
    fidl::StringPtr firebase_api_key,
    GetAppFirebaseTokenCallback callback) {
  FirebaseTokenPtr fb_token = fuchsia::auth::FirebaseToken::New();
  fb_token->id_token =
      std::string(firebase_api_key) + ":fbt_" + GenerateRandomString();
  fb_token->email = GenerateRandomString() + "@firebase.example.com";
  fb_token->local_id = "local_id_" + GenerateRandomString();
  fb_token->expires_in = 3600;

  callback(AuthProviderStatus::OK, std::move(fb_token));
}

void DevAuthProviderImpl::RevokeAppOrPersistentCredential(
    fidl::StringPtr credential,
    RevokeAppOrPersistentCredentialCallback callback) {
  callback(AuthProviderStatus::OK);
}

}  // namespace dev_auth_provider
}  // namespace auth

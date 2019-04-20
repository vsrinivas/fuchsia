// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/identity/bin/google_auth_provider/google_auth_provider_impl.h"

#include "lib/callback/capture.h"
#include "lib/callback/set_when_called.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/gtest/test_loop_fixture.h"
#include "lib/network_wrapper/fake_network_wrapper.h"
#include "peridot/lib/rapidjson/rapidjson.h"
#include "src/identity/bin/google_auth_provider/settings.h"
#include "src/lib/fxl/macros.h"

namespace google_auth_provider {
namespace {

using fuchsia::auth::AuthProviderStatus;
using fuchsia::auth::AuthTokenPtr;

class GoogleAuthProviderImplTest : public gtest::TestLoopFixture {
 public:
  GoogleAuthProviderImplTest()
      : network_wrapper_(dispatcher()),
        context_(sys::ComponentContext::Create().get()),
        google_auth_provider_impl_(dispatcher(), context_, &network_wrapper_,
                                   {}, auth_provider_.NewRequest()) {}

  ~GoogleAuthProviderImplTest() override {}

 protected:
  network_wrapper::FakeNetworkWrapper network_wrapper_;
  sys::ComponentContext* context_;
  fuchsia::auth::AuthProviderPtr auth_provider_;
  GoogleAuthProviderImpl google_auth_provider_impl_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(GoogleAuthProviderImplTest);
};

TEST_F(GoogleAuthProviderImplTest, EmptyWhenClientDisconnected) {
  bool on_empty_called = false;
  google_auth_provider_impl_.set_on_empty([this, &on_empty_called] {
    on_empty_called = true;
    QuitLoop();
  });
  auth_provider_.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(on_empty_called);
}

TEST_F(GoogleAuthProviderImplTest, GetAppAccessTokenSuccess) {
  bool callback_called = false;
  auto status = AuthProviderStatus::INTERNAL_ERROR;
  AuthTokenPtr access_token;
  std::vector<std::string> scopes;
  scopes.push_back("https://www.googleapis.com/auth/gmail.modify");
  scopes.push_back("https://www.googleapis.com/auth/userinfo.email");

  rapidjson::Document ok_response;
  ok_response.Parse(
      "{\"access_token\":\"test_at_token\", \"expires_in\":3600}");
  network_wrapper_.SetStringResponse(
      modular::JsonValueToPrettyString(ok_response), 200);

  auth_provider_->GetAppAccessToken(
      "credential", "test_client_id", std::move(scopes),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status,
                        &access_token));

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(status, AuthProviderStatus::OK);
  EXPECT_FALSE(access_token == NULL);
  EXPECT_EQ(access_token->token_type, fuchsia::auth::TokenType::ACCESS_TOKEN);
  EXPECT_EQ(access_token->token, "test_at_token");
  EXPECT_EQ(access_token->expires_in, 3600u);
}

TEST_F(GoogleAuthProviderImplTest, GetAppAccessTokenBadRequestError) {
  bool callback_called = false;
  auto status = AuthProviderStatus::INTERNAL_ERROR;
  fuchsia::auth::AuthTokenPtr access_token;
  std::vector<std::string> scopes;
  scopes.push_back("https://www.googleapis.com/auth/gmail.modify");

  auth_provider_->GetAppAccessToken(
      "", "", std::move(scopes),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status,
                        &access_token));

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(status, AuthProviderStatus::BAD_REQUEST);
  EXPECT_TRUE(access_token == NULL);
}

TEST_F(GoogleAuthProviderImplTest, GetAppAccessTokenInvalidClientError) {
  bool callback_called = false;
  auto status = AuthProviderStatus::INTERNAL_ERROR;
  AuthTokenPtr access_token;
  std::vector<std::string> scopes;
  scopes.push_back("https://www.googleapis.com/auth/gmail.modify");
  scopes.push_back("https://www.googleapis.com/auth/userinfo.email");

  rapidjson::Document ok_response;
  ok_response.Parse("{\"error\":\"invalid_client\"}");
  network_wrapper_.SetStringResponse(
      modular::JsonValueToPrettyString(ok_response), 401);

  auth_provider_->GetAppAccessToken(
      "credential", "invalid_client_id", std::move(scopes),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status,
                        &access_token));

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(status, AuthProviderStatus::OAUTH_SERVER_ERROR);
  EXPECT_TRUE(access_token == NULL);
}

TEST_F(GoogleAuthProviderImplTest, GetAppAccessTokenInvalidUserError) {
  bool callback_called = false;
  auto status = AuthProviderStatus::INTERNAL_ERROR;
  AuthTokenPtr access_token;
  std::vector<std::string> scopes;
  scopes.push_back("https://www.googleapis.com/auth/gmail.modify");
  scopes.push_back("https://www.googleapis.com/auth/userinfo.email");

  rapidjson::Document ok_response;
  ok_response.Parse("{\"error\":\"invalid_credential\"}");
  network_wrapper_.SetStringResponse(
      modular::JsonValueToPrettyString(ok_response), 401);

  auth_provider_->GetAppAccessToken(
      "invalid_credential", "test_client_id", std::move(scopes),
      callback::Capture(callback::SetWhenCalled(&callback_called), &status,
                        &access_token));

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(status, AuthProviderStatus::OAUTH_SERVER_ERROR);
  EXPECT_TRUE(access_token == NULL);
}

TEST_F(GoogleAuthProviderImplTest, GetAppIdTokenSuccess) {
  bool callback_called = false;
  auto status = AuthProviderStatus::INTERNAL_ERROR;
  AuthTokenPtr id_token;

  rapidjson::Document ok_response;
  ok_response.Parse("{\"id_token\":\"test_id_token\", \"expires_in\":3600}");
  network_wrapper_.SetStringResponse(
      modular::JsonValueToPrettyString(ok_response), 200);

  auth_provider_->GetAppIdToken(
      "credential", "test_audience",
      callback::Capture(callback::SetWhenCalled(&callback_called), &status,
                        &id_token));

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(status, AuthProviderStatus::OK);
  EXPECT_FALSE(id_token == NULL);
  EXPECT_EQ(id_token->token_type, fuchsia::auth::TokenType::ID_TOKEN);
  EXPECT_EQ(id_token->token, "test_id_token");
  EXPECT_EQ(id_token->expires_in, 3600u);
}

TEST_F(GoogleAuthProviderImplTest, GetAppIdTokenBadRequestError) {
  bool callback_called = false;
  auto status = AuthProviderStatus::INTERNAL_ERROR;
  AuthTokenPtr id_token;

  auth_provider_->GetAppIdToken(
      "", "test_audience",
      callback::Capture(callback::SetWhenCalled(&callback_called), &status,
                        &id_token));
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(status, AuthProviderStatus::BAD_REQUEST);
  EXPECT_TRUE(id_token == NULL);
}

TEST_F(GoogleAuthProviderImplTest, GetAppIdTokenInvalidAudienceError) {
  bool callback_called = false;
  auto status = AuthProviderStatus::INTERNAL_ERROR;
  AuthTokenPtr id_token;

  rapidjson::Document ok_response;
  ok_response.Parse("{\"error\":\"invalid_client\"}");
  network_wrapper_.SetStringResponse(
      modular::JsonValueToPrettyString(ok_response), 401);

  auth_provider_->GetAppIdToken(
      "credential", "invalid_audience",
      callback::Capture(callback::SetWhenCalled(&callback_called), &status,
                        &id_token));

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(status, AuthProviderStatus::OAUTH_SERVER_ERROR);
  EXPECT_TRUE(id_token == NULL);
}

TEST_F(GoogleAuthProviderImplTest, GetAppIdTokenInvalidUserError) {
  bool callback_called = false;
  auto status = AuthProviderStatus::INTERNAL_ERROR;
  AuthTokenPtr id_token;

  rapidjson::Document ok_response;
  ok_response.Parse("{\"error\":\"invalid_credential\"}");
  network_wrapper_.SetStringResponse(
      modular::JsonValueToPrettyString(ok_response), 401);

  auth_provider_->GetAppIdToken(
      "invalid_credential", "audience",
      callback::Capture(callback::SetWhenCalled(&callback_called), &status,
                        &id_token));

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(status, AuthProviderStatus::OAUTH_SERVER_ERROR);
  EXPECT_TRUE(id_token == NULL);
}

TEST_F(GoogleAuthProviderImplTest, GetAppFirebaseTokenSuccess) {
  bool callback_called = false;
  auto status = AuthProviderStatus::INTERNAL_ERROR;
  fuchsia::auth::FirebaseTokenPtr fb_token;

  rapidjson::Document ok_response;
  ok_response.Parse(
      "{\"idToken\":\"test_fb_token\", \"localId\":\"test123\",\
                      \"email\":\"foo@example.com\", \"expiresIn\":\"3600\"}");
  network_wrapper_.SetStringResponse(
      modular::JsonValueToPrettyString(ok_response), 200);

  auth_provider_->GetAppFirebaseToken(
      "test_id_token", "test_firebase_api_key",
      callback::Capture(callback::SetWhenCalled(&callback_called), &status,
                        &fb_token));

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(status, AuthProviderStatus::OK);
  EXPECT_FALSE(fb_token == NULL);
  EXPECT_EQ(fb_token->id_token, "test_fb_token");
  EXPECT_EQ(fb_token->local_id, "test123");
  EXPECT_EQ(fb_token->email, "foo@example.com");
  EXPECT_EQ(fb_token->expires_in, 3600u);
}

TEST_F(GoogleAuthProviderImplTest, GetAppFirebaseTokenBadRequestError) {
  bool callback_called = false;
  auto status = AuthProviderStatus::INTERNAL_ERROR;
  fuchsia::auth::FirebaseTokenPtr fb_token;

  auth_provider_->GetAppFirebaseToken(
      "", "",
      callback::Capture(callback::SetWhenCalled(&callback_called), &status,
                        &fb_token));

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(status, AuthProviderStatus::BAD_REQUEST);
  EXPECT_TRUE(fb_token == NULL);
}

TEST_F(GoogleAuthProviderImplTest, RevokeAppOrPersistentCredentialUnsupported) {
  bool callback_called = false;
  auto status = AuthProviderStatus::INTERNAL_ERROR;

  auth_provider_->RevokeAppOrPersistentCredential(
      "",
      callback::Capture(callback::SetWhenCalled(&callback_called), &status));

  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(status, AuthProviderStatus::BAD_REQUEST);
}

}  // namespace
}  // namespace google_auth_provider

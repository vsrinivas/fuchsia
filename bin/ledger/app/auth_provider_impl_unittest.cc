// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/auth_provider_impl.h"

#include "apps/ledger/src/callback/capture.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "apps/modular/services/auth/token_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/functional/make_copyable.h"

namespace ledger {

namespace {

class TestTokenProvider : public modular::auth::TokenProvider {
 public:
  TestTokenProvider(ftl::RefPtr<ftl::TaskRunner> task_runner)
      : task_runner_(task_runner) {}

  // modular::auth::TokenProvider:
  void GetAccessToken(const GetAccessTokenCallback& callback) override {
    FTL_NOTIMPLEMENTED();
  }

  void GetIdToken(const GetIdTokenCallback& callback) override {
    FTL_NOTIMPLEMENTED();
  }

  void GetFirebaseAuthToken(
      const fidl::String& firebase_api_key,
      const GetFirebaseAuthTokenCallback& callback) override {
    task_runner_->PostTask(ftl::MakeCopyable([
      token_to_return = std::move(token_to_return),
      callback = std::move(callback)
    ]() mutable { callback(std::move(token_to_return)); }));
  }

  void GetClientId(const GetClientIdCallback& callback) override {
    FTL_NOTIMPLEMENTED();
  }

  void Set(std::string id_token, std::string local_id, std::string email) {
    token_to_return = modular::auth::FirebaseToken::New();
    token_to_return->id_token = id_token;
    token_to_return->local_id = local_id;
    token_to_return->email = email;
  }

  void SetNull() { token_to_return = nullptr; }

  modular::auth::FirebaseTokenPtr token_to_return;

 private:
  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  FTL_DISALLOW_COPY_AND_ASSIGN(TestTokenProvider);
};

class AuthProviderImplTest : public test::TestWithMessageLoop {
 public:
  AuthProviderImplTest()
      : token_provider_(message_loop_.task_runner()),
        token_provider_binding_(&token_provider_) {}
  ~AuthProviderImplTest() override {}

 protected:
  TestTokenProvider token_provider_;
  fidl::Binding<modular::auth::TokenProvider> token_provider_binding_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(AuthProviderImplTest);
};

TEST_F(AuthProviderImplTest, GetFirebaseToken) {
  AuthProviderImpl auth_provider(message_loop_.task_runner(), "api_key",
                                 modular::auth::TokenProviderPtr::Create(
                                     token_provider_binding_.NewBinding()));

  token_provider_.Set("this is a token", "some id", "me@example.com");

  cloud_sync::AuthStatus auth_status;
  std::string firebase_token;
  auth_provider.GetFirebaseToken(
      callback::Capture(MakeQuitTask(), &auth_status, &firebase_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_sync::AuthStatus::OK, auth_status);
  EXPECT_EQ("this is a token", firebase_token);
}

TEST_F(AuthProviderImplTest, GetFirebaseTokenErrorIfEmpty) {
  AuthProviderImpl auth_provider(message_loop_.task_runner(), "api_key",
                                 modular::auth::TokenProviderPtr::Create(
                                     token_provider_binding_.NewBinding()));

  token_provider_.Set("", "", "");
  cloud_sync::AuthStatus auth_status;
  std::string firebase_token;
  auth_provider.GetFirebaseToken(
      callback::Capture(MakeQuitTask(), &auth_status, &firebase_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_sync::AuthStatus::ERROR, auth_status);

  token_provider_.SetNull();
  auth_provider.GetFirebaseToken(
      callback::Capture(MakeQuitTask(), &auth_status, &firebase_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_sync::AuthStatus::ERROR, auth_status);
}

TEST_F(AuthProviderImplTest, GetFirebaseUserId) {
  AuthProviderImpl auth_provider(message_loop_.task_runner(), "api_key",
                                 modular::auth::TokenProviderPtr::Create(
                                     token_provider_binding_.NewBinding()));

  token_provider_.Set("this is a token", "some id", "me@example.com");

  cloud_sync::AuthStatus auth_status;
  std::string firebase_user_id;
  auth_provider.GetFirebaseUserId(
      callback::Capture(MakeQuitTask(), &auth_status, &firebase_user_id));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_sync::AuthStatus::OK, auth_status);
  EXPECT_EQ("some id", firebase_user_id);
}

TEST_F(AuthProviderImplTest, GetFirebaseUserIdErrorIfEmpty) {
  AuthProviderImpl auth_provider(message_loop_.task_runner(), "api_key",
                                 modular::auth::TokenProviderPtr::Create(
                                     token_provider_binding_.NewBinding()));

  token_provider_.Set("", "", "");
  cloud_sync::AuthStatus auth_status;
  std::string firebase_user_id;
  auth_provider.GetFirebaseUserId(
      callback::Capture(MakeQuitTask(), &auth_status, &firebase_user_id));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_sync::AuthStatus::ERROR, auth_status);
  EXPECT_EQ("", firebase_user_id);

  token_provider_.SetNull();
  auth_provider.GetFirebaseUserId(
      callback::Capture(MakeQuitTask(), &auth_status, &firebase_user_id));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_sync::AuthStatus::ERROR, auth_status);
  EXPECT_EQ("", firebase_user_id);
}

// Verifies that if configured with empty API key, the auth provider returns
// empty tokens. This allows to run benchmarks against unaunthenticated cloud.
TEST_F(AuthProviderImplTest, NoApiKey) {
  AuthProviderImpl auth_provider(message_loop_.task_runner(), "",
                                 modular::auth::TokenProviderPtr::Create(
                                     token_provider_binding_.NewBinding()));

  cloud_sync::AuthStatus auth_status;
  std::string firebase_token;
  auth_provider.GetFirebaseToken(
      callback::Capture(MakeQuitTask(), &auth_status, &firebase_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_sync::AuthStatus::OK, auth_status);
  EXPECT_EQ("", firebase_token);

  std::string firebase_user_id;
  auth_provider.GetFirebaseToken(
      callback::Capture(MakeQuitTask(), &auth_status, &firebase_user_id));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_sync::AuthStatus::OK, auth_status);
  EXPECT_EQ("", firebase_user_id);
}

}  // namespace

}  // namespace ledger

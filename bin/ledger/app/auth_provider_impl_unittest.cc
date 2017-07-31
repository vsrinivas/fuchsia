// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/app/auth_provider_impl.h"

#include "apps/ledger/src/backoff/test/test_backoff.h"
#include "apps/ledger/src/callback/capture.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "apps/modular/services/auth/token_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/ftl/functional/make_copyable.h"

namespace ledger {

namespace {

class TestTokenProvider : public modular::auth::TokenProvider {
 public:
  explicit TestTokenProvider(ftl::RefPtr<ftl::TaskRunner> task_runner)
      : task_runner_(task_runner) {
    error_to_return = modular::auth::AuthErr::New();
    error_to_return->status = modular::auth::Status::OK;
    error_to_return->message = "";
  }

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
      token_to_return = token_to_return.Clone(),
      error_to_return = error_to_return.Clone(), callback = std::move(callback)
    ]() mutable {
      callback(std::move(token_to_return), std::move(error_to_return));
    }));
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
  modular::auth::AuthErrPtr error_to_return;

 private:
  ftl::RefPtr<ftl::TaskRunner> task_runner_;
  FTL_DISALLOW_COPY_AND_ASSIGN(TestTokenProvider);
};

class AuthProviderImplTest : public test::TestWithMessageLoop {
 public:
  AuthProviderImplTest()
      : token_provider_(message_loop_.task_runner()),
        token_provider_binding_(&token_provider_),
        auth_provider_(message_loop_.task_runner(),
                       "api_key",
                       modular::auth::TokenProviderPtr::Create(
                           token_provider_binding_.NewBinding()),
                       InitBackoff()) {}
  ~AuthProviderImplTest() override {}

 protected:
  std::unique_ptr<backoff::Backoff> InitBackoff() {
    auto backoff = std::make_unique<backoff::test::TestBackoff>();
    backoff_ = backoff.get();
    return backoff;
  }

  TestTokenProvider token_provider_;
  fidl::Binding<modular::auth::TokenProvider> token_provider_binding_;
  AuthProviderImpl auth_provider_;
  backoff::test::TestBackoff* backoff_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(AuthProviderImplTest);
};

TEST_F(AuthProviderImplTest, GetFirebaseToken) {
  token_provider_.Set("this is a token", "some id", "me@example.com");

  cloud_sync::AuthStatus auth_status;
  std::string firebase_token;
  auth_provider_.GetFirebaseToken(
      callback::Capture(MakeQuitTask(), &auth_status, &firebase_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_sync::AuthStatus::OK, auth_status);
  EXPECT_EQ("this is a token", firebase_token);
}

TEST_F(AuthProviderImplTest, GetFirebaseTokenRetryOnError) {
  token_provider_.Set("this is a token", "some id", "me@example.com");

  cloud_sync::AuthStatus auth_status;
  std::string firebase_token;
  token_provider_.error_to_return->status =
      modular::auth::Status::NETWORK_ERROR;
  backoff_->SetOnGetNext(MakeQuitTask());
  bool called = false;
  auth_provider_.GetFirebaseToken(
      [this, &called, &auth_status, &firebase_token](auto status, auto token) {
        called = true;
        auth_status = status;
        firebase_token = std::move(token);
        message_loop_.PostQuitTask();
      });
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_FALSE(called);
  EXPECT_EQ(1, backoff_->get_next_count);
  EXPECT_EQ(0, backoff_->reset_count);

  token_provider_.error_to_return->status = modular::auth::Status::OK;
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(called);
  EXPECT_EQ(cloud_sync::AuthStatus::OK, auth_status);
  EXPECT_EQ("this is a token", firebase_token);
  EXPECT_EQ(1, backoff_->get_next_count);
  EXPECT_EQ(1, backoff_->reset_count);
}

TEST_F(AuthProviderImplTest, GetFirebaseUserId) {
  token_provider_.Set("this is a token", "some id", "me@example.com");

  cloud_sync::AuthStatus auth_status;
  std::string firebase_user_id;
  auth_provider_.GetFirebaseUserId(
      callback::Capture(MakeQuitTask(), &auth_status, &firebase_user_id));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(cloud_sync::AuthStatus::OK, auth_status);
  EXPECT_EQ("some id", firebase_user_id);
}

TEST_F(AuthProviderImplTest, GetFirebaseUserIdRetryOnError) {
  token_provider_.Set("this is a token", "some id", "me@example.com");

  cloud_sync::AuthStatus auth_status;
  std::string firebase_id;
  token_provider_.error_to_return->status =
      modular::auth::Status::NETWORK_ERROR;
  backoff_->SetOnGetNext(MakeQuitTask());
  bool called = false;
  auth_provider_.GetFirebaseUserId(
      [this, &called, &auth_status, &firebase_id](auto status, auto id) {
        called = true;
        auth_status = status;
        firebase_id = std::move(id);
        message_loop_.PostQuitTask();
      });
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_FALSE(called);
  EXPECT_EQ(1, backoff_->get_next_count);
  EXPECT_EQ(0, backoff_->reset_count);

  token_provider_.error_to_return->status = modular::auth::Status::OK;
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(called);
  EXPECT_EQ(cloud_sync::AuthStatus::OK, auth_status);
  EXPECT_EQ("some id", firebase_id);
  EXPECT_EQ(1, backoff_->get_next_count);
  EXPECT_EQ(1, backoff_->reset_count);
}

}  // namespace

}  // namespace ledger

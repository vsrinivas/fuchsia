// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/firebase_auth/firebase_auth_impl.h"

#include <utility>

#include "lib/auth/fidl/token_provider.fidl.h"
#include "lib/fidl/cpp/bindings/binding.h"
#include "lib/fxl/functional/make_copyable.h"
#include "peridot/bin/ledger/test/test_with_message_loop.h"
#include "peridot/lib/backoff/test/test_backoff.h"
#include "peridot/lib/callback/capture.h"
#include "peridot/lib/firebase_auth/test/test_token_provider.h"

namespace firebase_auth {

namespace {

class FirebaseAuthImplTest : public ::test::TestWithMessageLoop {
 public:
  FirebaseAuthImplTest()
      : token_provider_(message_loop_.task_runner()),
        token_provider_binding_(&token_provider_),
        firebase_auth_(message_loop_.task_runner(),
                       "api_key",
                       modular::auth::TokenProviderPtr::Create(
                           token_provider_binding_.NewBinding()),
                       InitBackoff()) {}
  ~FirebaseAuthImplTest() override {}

 protected:
  std::unique_ptr<backoff::Backoff> InitBackoff() {
    auto backoff = std::make_unique<backoff::test::TestBackoff>();
    backoff_ = backoff.get();
    return backoff;
  }

  test::TestTokenProvider token_provider_;
  fidl::Binding<modular::auth::TokenProvider> token_provider_binding_;
  FirebaseAuthImpl firebase_auth_;
  backoff::test::TestBackoff* backoff_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(FirebaseAuthImplTest);
};

TEST_F(FirebaseAuthImplTest, GetFirebaseToken) {
  token_provider_.Set("this is a token", "some id", "me@example.com");

  AuthStatus auth_status;
  std::string firebase_token;
  firebase_auth_.GetFirebaseToken(
      callback::Capture(MakeQuitTask(), &auth_status, &firebase_token));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(AuthStatus::OK, auth_status);
  EXPECT_EQ("this is a token", firebase_token);
}

TEST_F(FirebaseAuthImplTest, GetFirebaseTokenRetryOnError) {
  token_provider_.Set("this is a token", "some id", "me@example.com");

  AuthStatus auth_status;
  std::string firebase_token;
  token_provider_.error_to_return->status =
      modular::auth::Status::NETWORK_ERROR;
  backoff_->SetOnGetNext(MakeQuitTask());
  bool called = false;
  firebase_auth_.GetFirebaseToken(
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
  EXPECT_EQ(AuthStatus::OK, auth_status);
  EXPECT_EQ("this is a token", firebase_token);
  EXPECT_EQ(1, backoff_->get_next_count);
  EXPECT_EQ(1, backoff_->reset_count);
}

TEST_F(FirebaseAuthImplTest, GetFirebaseUserId) {
  token_provider_.Set("this is a token", "some id", "me@example.com");

  AuthStatus auth_status;
  std::string firebase_user_id;
  firebase_auth_.GetFirebaseUserId(
      callback::Capture(MakeQuitTask(), &auth_status, &firebase_user_id));
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_EQ(AuthStatus::OK, auth_status);
  EXPECT_EQ("some id", firebase_user_id);
}

TEST_F(FirebaseAuthImplTest, GetFirebaseUserIdRetryOnError) {
  token_provider_.Set("this is a token", "some id", "me@example.com");

  AuthStatus auth_status;
  std::string firebase_id;
  token_provider_.error_to_return->status =
      modular::auth::Status::NETWORK_ERROR;
  backoff_->SetOnGetNext(MakeQuitTask());
  bool called = false;
  firebase_auth_.GetFirebaseUserId(
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
  EXPECT_EQ(AuthStatus::OK, auth_status);
  EXPECT_EQ("some id", firebase_id);
  EXPECT_EQ(1, backoff_->get_next_count);
  EXPECT_EQ(1, backoff_->reset_count);
}

}  // namespace

}  // namespace firebase_auth

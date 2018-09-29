// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/firebase_auth/firebase_auth_impl.h"

#include <utility>

#include <fuchsia/auth/cpp/fidl.h>
#include <lib/backoff/testing/test_backoff.h>
#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/fxl/functional/make_copyable.h>
#include <lib/gtest/test_loop_fixture.h>

#include "peridot/lib/firebase_auth/testing/test_token_manager.h"

namespace firebase_auth {

namespace {

class MockCobaltLogger : public cobalt::CobaltLogger {
 public:
  ~MockCobaltLogger() override = default;

  MockCobaltLogger(int* called) : called_(called) {}

  void LogEvent(uint32_t metric_id, uint32_t event_type_index) override {}
  void LogEventCount(uint32_t metric_id, uint32_t event_type_index,
                     const std::string& component, zx::duration period_duration,
                     int64_t count) override {
    EXPECT_EQ(4u, metric_id);
    // The value should contain the client name.
    EXPECT_TRUE(component.find("firebase-test") != std::string::npos);
    *called_ += 1;
  }
  void LogElapsedTime(uint32_t metric_id, uint32_t event_type_index,
                      const std::string& component,
                      zx::duration elapsed_time) override {}
  void LogFrameRate(uint32_t metric_id, uint32_t event_type_index,
                    const std::string& component, float fps) override {}
  void LogMemoryUsage(uint32_t metric_id, uint32_t event_type_index,
                      const std::string& component, int64_t bytes) override {}
  void LogString(uint32_t metric_id, const std::string& s) override {}
  void StartTimer(uint32_t metric_id, uint32_t event_type_index,
                  const std::string& component, const std::string& timer_id,
                  zx::time timestamp, zx::duration timeout) override {}
  void EndTimer(const std::string& timer_id, zx::time timestamp,
                zx::duration timeout) override {}
  void LogIntHistogram(
      uint32_t metric_id, uint32_t event_type_index,
      const std::string& component,
      std::vector<fuchsia::cobalt::HistogramBucket> histogram) override {}
  void LogCustomEvent(
      uint32_t metric_id,
      std::vector<fuchsia::cobalt::CustomEventValue> event_values) override {}

 private:
  int* called_;
};

class FirebaseAuthV2ImplTest : public gtest::TestLoopFixture {
 public:
  FirebaseAuthV2ImplTest()
      : token_manager_(dispatcher()),
        token_manager_binding_(&token_manager_),
        firebase_auth_(
            {"api_key", "firebase-test", 1}, dispatcher(), nullptr,
            token_manager_binding_.NewBinding().Bind(), InitBackoff(),
            std::make_unique<MockCobaltLogger>(&report_observation_count_)) {}

  ~FirebaseAuthV2ImplTest() override {}

 protected:
  std::unique_ptr<backoff::Backoff> InitBackoff() {
    // TODO(LE-630): Don't use a backoff duration of 0.
    auto backoff = std::make_unique<backoff::TestBackoff>(zx::sec(0));
    backoff_ = backoff.get();
    return backoff;
  }

  TestTokenManager token_manager_;
  fidl::Binding<fuchsia::auth::TokenManager> token_manager_binding_;
  FirebaseAuthImpl firebase_auth_;
  int report_observation_count_ = 0;
  backoff::TestBackoff* backoff_;

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(FirebaseAuthV2ImplTest);
};

TEST_F(FirebaseAuthV2ImplTest, GetFirebaseToken) {
  token_manager_.Set("this is a token", "some id", "me@example.com");

  bool called;
  AuthStatus auth_status;
  std::string firebase_token;
  firebase_auth_.GetFirebaseToken(callback::Capture(
      callback::SetWhenCalled(&called), &auth_status, &firebase_token));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(AuthStatus::OK, auth_status);
  EXPECT_EQ("this is a token", firebase_token);
  EXPECT_EQ(0, report_observation_count_);
}

TEST_F(FirebaseAuthV2ImplTest, GetFirebaseTokenRetryOnError) {
  bool called;
  AuthStatus auth_status;
  std::string firebase_token;
  token_manager_.SetError(fuchsia::auth::Status::NETWORK_ERROR);
  backoff_->SetOnGetNext(QuitLoopClosure());
  firebase_auth_.GetFirebaseToken(callback::Capture(
      callback::SetWhenCalled(&called), &auth_status, &firebase_token));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);
  EXPECT_EQ(1, backoff_->get_next_count);
  EXPECT_EQ(0, backoff_->reset_count);
  EXPECT_EQ(0, report_observation_count_);

  token_manager_.Set("this is a token", "some id", "me@example.com");
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(AuthStatus::OK, auth_status);
  EXPECT_EQ("this is a token", firebase_token);
  EXPECT_EQ(1, backoff_->get_next_count);
  EXPECT_EQ(1, backoff_->reset_count);
  EXPECT_EQ(0, report_observation_count_);
}

TEST_F(FirebaseAuthV2ImplTest, GetFirebaseUserId) {
  token_manager_.Set("this is a token", "some id", "me@example.com");

  bool called;
  AuthStatus auth_status;
  std::string firebase_user_id;
  firebase_auth_.GetFirebaseUserId(callback::Capture(
      callback::SetWhenCalled(&called), &auth_status, &firebase_user_id));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(AuthStatus::OK, auth_status);
  EXPECT_EQ("some id", firebase_user_id);
  EXPECT_EQ(0, report_observation_count_);
}

TEST_F(FirebaseAuthV2ImplTest, GetFirebaseUserIdRetryOnError) {
  bool called;
  AuthStatus auth_status;
  std::string firebase_id;
  token_manager_.SetError(fuchsia::auth::Status::NETWORK_ERROR);
  backoff_->SetOnGetNext(QuitLoopClosure());
  firebase_auth_.GetFirebaseUserId(callback::Capture(
      callback::SetWhenCalled(&called), &auth_status, &firebase_id));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);
  EXPECT_EQ(1, backoff_->get_next_count);
  EXPECT_EQ(0, backoff_->reset_count);
  EXPECT_EQ(0, report_observation_count_);

  token_manager_.Set("this is a token", "some id", "me@example.com");
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(AuthStatus::OK, auth_status);
  EXPECT_EQ("some id", firebase_id);
  EXPECT_EQ(1, backoff_->get_next_count);
  EXPECT_EQ(1, backoff_->reset_count);
  EXPECT_EQ(0, report_observation_count_);
}

TEST_F(FirebaseAuthV2ImplTest, GetFirebaseUserIdMaxRetry) {
  bool called;
  AuthStatus auth_status;
  token_manager_.SetError(fuchsia::auth::Status::NETWORK_ERROR);
  backoff_->SetOnGetNext(QuitLoopClosure());
  firebase_auth_.GetFirebaseUserId(callback::Capture(
      callback::SetWhenCalled(&called), &auth_status, &std::ignore));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);
  EXPECT_EQ(1, backoff_->get_next_count);
  EXPECT_EQ(0, backoff_->reset_count);
  EXPECT_EQ(0, report_observation_count_);

  // Exceeding the maximum number of retriable errors returns an error.
  token_manager_.SetError(fuchsia::auth::Status::INTERNAL_ERROR);
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(AuthStatus::ERROR, auth_status);
  EXPECT_EQ(1, backoff_->get_next_count);
  EXPECT_EQ(1, backoff_->reset_count);
  EXPECT_EQ(1, report_observation_count_);
}

TEST_F(FirebaseAuthV2ImplTest, GetFirebaseUserIdNonRetriableError) {
  bool called;
  AuthStatus auth_status;
  token_manager_.SetError(fuchsia::auth::Status::INVALID_REQUEST);
  backoff_->SetOnGetNext(QuitLoopClosure());
  firebase_auth_.GetFirebaseUserId(callback::Capture(
      callback::SetWhenCalled(&called), &auth_status, &std::ignore));
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
  EXPECT_EQ(AuthStatus::ERROR, auth_status);
  EXPECT_EQ(0, backoff_->get_next_count);
  EXPECT_EQ(1, backoff_->reset_count);
  EXPECT_EQ(1, report_observation_count_);
}

}  // namespace

}  // namespace firebase_auth

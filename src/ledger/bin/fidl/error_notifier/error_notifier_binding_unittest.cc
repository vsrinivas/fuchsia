// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/callback/capture.h>
#include <lib/callback/set_when_called.h>
#include <lib/gtest/test_loop_fixture.h>

#include "gtest/gtest.h"
#include "src/ledger/bin/fidl/error_notifier/error_notifier_fidl_test.h"

namespace ledger {
namespace {

class ErrorNotifierTestErrorNotifierDelegateImpl
    : public fuchsia::ledger::errornotifiertest::
          ErrorNotifierTestErrorNotifierDelegate {
 public:
  int no_reponse_count() { return no_reponse_count_; }
  int empty_reponse_count() { return empty_reponse_count_; }
  int not_empty_reponse_count() { return not_empty_reponse_count_; }
  int parameter_received() { return parameter_received_; }
  fuchsia::ledger::Status& status_to_return() { return status_to_return_; }
  bool& delay_callback() { return delay_callback_; }
  void RunDelayedCallback() { delayed_callback_(); }

 private:
  // ErrorNotifierTestErrorNotifierDelegate implementation.
  void NoResponse(
      fit::function<void(fuchsia::ledger::Status)> callback) override {
    NoResponseWithParameter(1, std::move(callback));
  }
  void NoResponseWithParameter(
      int8_t input,
      fit::function<void(fuchsia::ledger::Status)> callback) override {
    parameter_received_ = input;
    ++no_reponse_count_;
    delayed_callback_ = [callback = std::move(callback),
                         status_to_return = status_to_return_] {
      callback(status_to_return);
    };
    if (!delay_callback_) {
      delayed_callback_();
    }
  }
  void EmptyResponse(
      fit::function<void(fuchsia::ledger::Status)> callback) override {
    EmptyResponseWithParameter(2, std::move(callback));
  }
  void EmptyResponseWithParameter(
      int8_t input,
      fit::function<void(fuchsia::ledger::Status)> callback) override {
    parameter_received_ = input;
    ++empty_reponse_count_;
    delayed_callback_ = [callback = std::move(callback),
                         status_to_return = status_to_return_] {
      callback(status_to_return);
    };
    if (!delay_callback_) {
      delayed_callback_();
    }
  }

  void NotEmptyResponse(::fit::function<void(fuchsia::ledger::Status, int8_t)>
                            callback) override {
    NotEmptyResponseWithParameter(3, std::move(callback));
  }
  void NotEmptyResponseWithParameter(
      int8_t input,
      fit::function<void(fuchsia::ledger::Status, int8_t)> callback) override {
    parameter_received_ = input;
    ++not_empty_reponse_count_;
    delayed_callback_ = [callback = std::move(callback),
                         status_to_return = status_to_return_] {
      callback(status_to_return, 1);
    };
    if (!delay_callback_) {
      delayed_callback_();
    }
  }

  int32_t no_reponse_count_ = 0;
  int32_t empty_reponse_count_ = 0;
  int32_t not_empty_reponse_count_ = 0;
  int32_t parameter_received_ = 0;
  fuchsia::ledger::Status status_to_return_ = fuchsia::ledger::Status::OK;
  bool delay_callback_ = false;
  fit::closure delayed_callback_;
};

class ErrorNotifierTest : public gtest::TestLoopFixture {
 protected:
  ErrorNotifierTest() : binding_(&impl_, ptr_.NewRequest()) {}

  ErrorNotifierTestErrorNotifierDelegateImpl impl_;
  fuchsia::ledger::errornotifiertest::ErrorNotifierTestPtr ptr_;
  ErrorNotifierBinding<fuchsia::ledger::errornotifiertest::
                           ErrorNotifierTestErrorNotifierDelegate>
      binding_;
};

TEST_F(ErrorNotifierTest, NoResponse) {
  zx_status_t status;
  bool error_called;

  ptr_.set_error_handler(
      callback::Capture(callback::SetWhenCalled(&error_called), &status));

  ptr_->NoResponse();
  RunLoopUntilIdle();
  EXPECT_EQ(1, impl_.no_reponse_count());
  EXPECT_TRUE(ptr_);
  EXPECT_FALSE(error_called);

  impl_.status_to_return() = fuchsia::ledger::Status::IO_ERROR;
  ptr_->NoResponse();
  RunLoopUntilIdle();
  EXPECT_EQ(2, impl_.no_reponse_count());
  EXPECT_FALSE(ptr_);
  EXPECT_TRUE(error_called);
  EXPECT_EQ(static_cast<zx_status_t>(fuchsia::ledger::Status::IO_ERROR),
            status);
}

TEST_F(ErrorNotifierTest, NoResponseWithParameter) {
  ptr_->NoResponseWithParameter(42);
  RunLoopUntilIdle();
  EXPECT_EQ(1, impl_.no_reponse_count());
  EXPECT_EQ(42, impl_.parameter_received());
}

TEST_F(ErrorNotifierTest, NoResponseSync) {
  impl_.delay_callback() = true;

  bool sync_called;
  ptr_->NoResponse();
  ptr_->Sync(callback::SetWhenCalled(&sync_called));

  RunLoopUntilIdle();
  EXPECT_FALSE(sync_called);

  impl_.RunDelayedCallback();
  RunLoopUntilIdle();
  EXPECT_TRUE(sync_called);
}

TEST_F(ErrorNotifierTest, EmptyResponse) {
  zx_status_t status;
  bool error_called;
  bool callback_called;

  ptr_.set_error_handler(
      callback::Capture(callback::SetWhenCalled(&error_called), &status));

  ptr_->EmptyResponse(callback::SetWhenCalled(&callback_called));
  RunLoopUntilIdle();
  EXPECT_EQ(1, impl_.empty_reponse_count());
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(ptr_);
  EXPECT_FALSE(error_called);

  impl_.status_to_return() = fuchsia::ledger::Status::IO_ERROR;
  ptr_->EmptyResponse(callback::SetWhenCalled(&callback_called));
  RunLoopUntilIdle();
  EXPECT_EQ(2, impl_.empty_reponse_count());
  EXPECT_FALSE(callback_called);
  EXPECT_FALSE(ptr_);
  EXPECT_TRUE(error_called);
  EXPECT_EQ(static_cast<zx_status_t>(fuchsia::ledger::Status::IO_ERROR),
            status);
}

TEST_F(ErrorNotifierTest, EmptyResponseWithParameter) {
  bool callback_called;

  ptr_->EmptyResponseWithParameter(42,
                                   callback::SetWhenCalled(&callback_called));
  RunLoopUntilIdle();
  EXPECT_EQ(1, impl_.empty_reponse_count());
  EXPECT_EQ(42, impl_.parameter_received());
  EXPECT_TRUE(callback_called);
}

TEST_F(ErrorNotifierTest, EmptyResponseSync) {
  impl_.delay_callback() = true;

  bool callback_called;
  bool sync_called;
  ptr_->EmptyResponse(callback::SetWhenCalled(&callback_called));
  ptr_->Sync(callback::SetWhenCalled(&sync_called));

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_FALSE(sync_called);

  impl_.RunDelayedCallback();
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(sync_called);
}

TEST_F(ErrorNotifierTest, NotEmptyResponse) {
  zx_status_t status;
  bool error_called;
  bool callback_called;
  int callback_value;

  ptr_.set_error_handler(
      callback::Capture(callback::SetWhenCalled(&error_called), &status));

  ptr_->NotEmptyResponse(callback::Capture(
      callback::SetWhenCalled(&callback_called), &callback_value));
  RunLoopUntilIdle();
  EXPECT_EQ(1, impl_.not_empty_reponse_count());
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(1, callback_value);
  EXPECT_TRUE(ptr_);
  EXPECT_FALSE(error_called);

  impl_.status_to_return() = fuchsia::ledger::Status::IO_ERROR;
  ptr_->NotEmptyResponse(callback::Capture(
      callback::SetWhenCalled(&callback_called), &std::ignore));
  RunLoopUntilIdle();
  EXPECT_EQ(2, impl_.not_empty_reponse_count());
  EXPECT_FALSE(callback_called);
  EXPECT_FALSE(ptr_);
  EXPECT_TRUE(error_called);
  EXPECT_EQ(static_cast<zx_status_t>(fuchsia::ledger::Status::IO_ERROR),
            status);
}

TEST_F(ErrorNotifierTest, NotEmptyResponseWithParameter) {
  bool callback_called;
  int callback_value;

  ptr_->NotEmptyResponseWithParameter(
      42, callback::Capture(callback::SetWhenCalled(&callback_called),
                            &callback_value));
  RunLoopUntilIdle();
  EXPECT_EQ(1, impl_.not_empty_reponse_count());
  EXPECT_EQ(42, impl_.parameter_received());
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(1, callback_value);
}

TEST_F(ErrorNotifierTest, NotEmptyResponseSync) {
  impl_.delay_callback() = true;

  bool callback_called;
  bool sync_called;
  ptr_->NotEmptyResponse(callback::Capture(
      callback::SetWhenCalled(&callback_called), &std::ignore));
  ptr_->Sync(callback::SetWhenCalled(&sync_called));

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_FALSE(sync_called);

  impl_.RunDelayedCallback();
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(sync_called);
}

TEST_F(ErrorNotifierTest, OnEmpty) {
  bool called;
  binding_.set_on_empty(callback::SetWhenCalled(&called));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);
  ptr_.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
}

TEST_F(ErrorNotifierTest, OnEmptyWithRunningOperation) {
  impl_.delay_callback() = true;
  bool called;
  binding_.set_on_empty(callback::SetWhenCalled(&called));
  ptr_->NoResponse();
  RunLoopUntilIdle();
  EXPECT_FALSE(called);
  ptr_.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(called);
  impl_.RunDelayedCallback();
  EXPECT_TRUE(called);
}

}  // namespace
}  // namespace ledger

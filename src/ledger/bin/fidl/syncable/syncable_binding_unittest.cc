// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/gtest/test_loop_fixture.h>

#include "gtest/gtest.h"
#include "src/ledger/bin/fidl/syncable/syncable_fidl_test.h"
#include "src/ledger/lib/callback/capture.h"
#include "src/ledger/lib/callback/set_when_called.h"

namespace ledger {
namespace {

class SyncableTestSyncableDelegateImpl
    : public fuchsia::ledger::syncabletest::SyncableTestSyncableDelegate {
 public:
  int no_reponse_count() { return no_reponse_count_; }
  int empty_reponse_count() { return empty_reponse_count_; }
  int not_empty_reponse_count() { return not_empty_reponse_count_; }
  int parameter_received() { return parameter_received_; }
  Status& status_to_return() { return status_to_return_; }
  bool& delay_callback() { return delay_callback_; }
  void RunDelayedCallback() { delayed_callback_(); }

 private:
  // SyncableTestSyncableDelegate implementation.
  void NoResponse(fit::function<void(Status)> callback) override {
    NoResponseWithParameter(1, std::move(callback));
  }
  void NoResponseWithParameter(int8_t input, fit::function<void(Status)> callback) override {
    parameter_received_ = input;
    ++no_reponse_count_;
    delayed_callback_ = [callback = std::move(callback), status_to_return = status_to_return_] {
      callback(status_to_return);
    };
    if (!delay_callback_) {
      delayed_callback_();
    }
  }
  void EmptyResponse(fit::function<void(Status)> callback) override {
    EmptyResponseWithParameter(2, std::move(callback));
  }
  void EmptyResponseWithParameter(int8_t input, fit::function<void(Status)> callback) override {
    parameter_received_ = input;
    ++empty_reponse_count_;
    delayed_callback_ = [callback = std::move(callback), status_to_return = status_to_return_] {
      callback(status_to_return);
    };
    if (!delay_callback_) {
      delayed_callback_();
    }
  }

  void NotEmptyResponse(::fit::function<void(Status, int8_t)> callback) override {
    NotEmptyResponseWithParameter(3, std::move(callback));
  }
  void NotEmptyResponseWithParameter(int8_t input,
                                     fit::function<void(Status, int8_t)> callback) override {
    parameter_received_ = input;
    ++not_empty_reponse_count_;
    delayed_callback_ = [callback = std::move(callback), status_to_return = status_to_return_] {
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
  Status status_to_return_ = Status::OK;
  bool delay_callback_ = false;
  fit::closure delayed_callback_;
};

class SyncableTest : public gtest::TestLoopFixture {
 protected:
  SyncableTest() : binding_(&impl_, ptr_.NewRequest()) {}

  SyncableTestSyncableDelegateImpl impl_;
  fuchsia::ledger::syncabletest::SyncableTestPtr ptr_;
  SyncableBinding<fuchsia::ledger::syncabletest::SyncableTestSyncableDelegate> binding_;
};

TEST_F(SyncableTest, NoResponse) {
  zx_status_t status;
  bool error_called;

  ptr_.set_error_handler(Capture(SetWhenCalled(&error_called), &status));

  ptr_->NoResponse();
  RunLoopUntilIdle();
  EXPECT_EQ(impl_.no_reponse_count(), 1);
  EXPECT_TRUE(ptr_);
  EXPECT_FALSE(error_called);

  impl_.status_to_return() = Status::IO_ERROR;
  ptr_->NoResponse();
  RunLoopUntilIdle();
  EXPECT_EQ(impl_.no_reponse_count(), 2);
  EXPECT_FALSE(ptr_);
  EXPECT_TRUE(error_called);
  EXPECT_EQ(status, ZX_ERR_IO);
}

TEST_F(SyncableTest, NoResponseWithParameter) {
  ptr_->NoResponseWithParameter(42);
  RunLoopUntilIdle();
  EXPECT_EQ(impl_.no_reponse_count(), 1);
  EXPECT_EQ(impl_.parameter_received(), 42);
}

TEST_F(SyncableTest, NoResponseSync) {
  impl_.delay_callback() = true;

  bool sync_called;
  ptr_->NoResponse();
  ptr_->Sync(SetWhenCalled(&sync_called));

  RunLoopUntilIdle();
  EXPECT_FALSE(sync_called);

  impl_.RunDelayedCallback();
  RunLoopUntilIdle();
  EXPECT_TRUE(sync_called);
}

TEST_F(SyncableTest, EmptyResponse) {
  zx_status_t status;
  bool error_called;
  bool callback_called;

  ptr_.set_error_handler(Capture(SetWhenCalled(&error_called), &status));

  ptr_->EmptyResponse(SetWhenCalled(&callback_called));
  RunLoopUntilIdle();
  EXPECT_EQ(impl_.empty_reponse_count(), 1);
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(ptr_);
  EXPECT_FALSE(error_called);

  impl_.status_to_return() = Status::IO_ERROR;
  ptr_->EmptyResponse(SetWhenCalled(&callback_called));
  RunLoopUntilIdle();
  EXPECT_EQ(impl_.empty_reponse_count(), 2);
  EXPECT_FALSE(callback_called);
  EXPECT_FALSE(ptr_);
  EXPECT_TRUE(error_called);
  EXPECT_EQ(status, ZX_ERR_IO);
}

TEST_F(SyncableTest, EmptyResponseWithParameter) {
  bool callback_called;

  ptr_->EmptyResponseWithParameter(42, SetWhenCalled(&callback_called));
  RunLoopUntilIdle();
  EXPECT_EQ(impl_.empty_reponse_count(), 1);
  EXPECT_EQ(impl_.parameter_received(), 42);
  EXPECT_TRUE(callback_called);
}

TEST_F(SyncableTest, EmptyResponseSync) {
  impl_.delay_callback() = true;

  bool callback_called;
  bool sync_called;
  ptr_->EmptyResponse(SetWhenCalled(&callback_called));
  ptr_->Sync(SetWhenCalled(&sync_called));

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_FALSE(sync_called);

  impl_.RunDelayedCallback();
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(sync_called);
}

TEST_F(SyncableTest, NotEmptyResponse) {
  zx_status_t status;
  bool error_called;
  bool callback_called;
  int callback_value;

  ptr_.set_error_handler(Capture(SetWhenCalled(&error_called), &status));

  ptr_->NotEmptyResponse(Capture(SetWhenCalled(&callback_called), &callback_value));
  RunLoopUntilIdle();
  EXPECT_EQ(impl_.not_empty_reponse_count(), 1);
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(callback_value, 1);
  EXPECT_TRUE(ptr_);
  EXPECT_FALSE(error_called);

  impl_.status_to_return() = Status::IO_ERROR;
  ptr_->NotEmptyResponse(Capture(SetWhenCalled(&callback_called), &std::ignore));
  RunLoopUntilIdle();
  EXPECT_EQ(impl_.not_empty_reponse_count(), 2);
  EXPECT_FALSE(callback_called);
  EXPECT_FALSE(ptr_);
  EXPECT_TRUE(error_called);
  EXPECT_EQ(status, ZX_ERR_IO);
}

TEST_F(SyncableTest, NotEmptyResponseWithParameter) {
  bool callback_called;
  int callback_value;

  ptr_->NotEmptyResponseWithParameter(42,
                                      Capture(SetWhenCalled(&callback_called), &callback_value));
  RunLoopUntilIdle();
  EXPECT_EQ(impl_.not_empty_reponse_count(), 1);
  EXPECT_EQ(impl_.parameter_received(), 42);
  EXPECT_TRUE(callback_called);
  EXPECT_EQ(callback_value, 1);
}

TEST_F(SyncableTest, NotEmptyResponseSync) {
  impl_.delay_callback() = true;

  bool callback_called;
  bool sync_called;
  ptr_->NotEmptyResponse(Capture(SetWhenCalled(&callback_called), &std::ignore));
  ptr_->Sync(SetWhenCalled(&sync_called));

  RunLoopUntilIdle();
  EXPECT_FALSE(callback_called);
  EXPECT_FALSE(sync_called);

  impl_.RunDelayedCallback();
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  EXPECT_TRUE(sync_called);
}

TEST_F(SyncableTest, OnDiscardable) {
  bool called;
  binding_.SetOnDiscardable(SetWhenCalled(&called));
  RunLoopUntilIdle();
  EXPECT_FALSE(called);
  ptr_.Unbind();
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
}

TEST_F(SyncableTest, OnDiscardableWithRunningOperation) {
  impl_.delay_callback() = true;
  bool called;
  binding_.SetOnDiscardable(SetWhenCalled(&called));
  ptr_->NoResponse();
  RunLoopUntilIdle();
  EXPECT_FALSE(called);
  ptr_.Unbind();
  RunLoopUntilIdle();
  EXPECT_FALSE(called);
  impl_.RunDelayedCallback();
  EXPECT_TRUE(called);
}

TEST_F(SyncableTest, OnDiscardableAfterError) {
  impl_.status_to_return() = Status::IO_ERROR;
  bool called;
  binding_.SetOnDiscardable(SetWhenCalled(&called));
  ptr_->NoResponse();
  RunLoopUntilIdle();
  EXPECT_TRUE(called);
}

}  // namespace
}  // namespace ledger

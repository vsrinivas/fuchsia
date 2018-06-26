// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/cobalt/app/timer_manager.h"

#include "lib/gtest/test_loop_fixture.h"
#include "third_party/googletest/googletest/include/gtest/gtest.h"

namespace cobalt {

const uint32_t kMetricId = 1;
const uint32_t kEncodingId = 1;
const uint32_t kTimeoutSec = 1;
const uint64_t kStartTimestamp = 10;
const uint64_t kEndTimestamp = 20;
const std::string kTimerId = "test_timer";

class TimerManagerTests : public ::gtest::TestLoopFixture {
 protected:
  void SetUp() override {
    timer_manager_.reset(new TimerManager(dispatcher()));
    test_clock_.reset(new wlan::TestClock());

    timer_manager_->SetClockForTesting(test_clock_);
    SetTimeSec(1);
  }

  void SetTimeSec(uint32_t time_s) {
    test_clock_->Set(zx::time(ZX_SEC(time_s)));
    AdvanceTimeTo(zx::time(ZX_SEC(time_s)));
  }

  std::unique_ptr<TimerManager> timer_manager_;
  std::shared_ptr<wlan::TestClock> test_clock_;
};

TEST_F(TimerManagerTests, ValidationEmptyTimerId) {
  EXPECT_FALSE(TimerManager::isValidTimerArguments(
      /*timer_id=*/fidl::StringPtr(""), kStartTimestamp, kTimeoutSec));
}

TEST_F(TimerManagerTests, ValidationTimeoutTooLong) {
  EXPECT_FALSE(TimerManager::isValidTimerArguments(
      /*timer_id=*/fidl::StringPtr(kTimerId), kStartTimestamp,
      /*timer_s=*/301));
}

TEST_F(TimerManagerTests, ValidationTimeoutTooShort) {
  EXPECT_FALSE(TimerManager::isValidTimerArguments(
      /*timer_id=*/fidl::StringPtr(kTimerId), kStartTimestamp, /*timer_s=*/0));
}

TEST_F(TimerManagerTests, ValidationNegativeTimeout) {
  EXPECT_FALSE(TimerManager::isValidTimerArguments(
      /*timer_id=*/fidl::StringPtr(kTimerId), -1, kTimeoutSec));
}

TEST_F(TimerManagerTests, ValidationValidArguments) {
  EXPECT_TRUE(TimerManager::isValidTimerArguments(
      /*timer_id=*/fidl::StringPtr(kTimerId), kStartTimestamp, kTimeoutSec));
}

TEST_F(TimerManagerTests, GetValidTimer) {
  std::unique_ptr<TimerVal> timer_val_ptr;

  auto status = timer_manager_->GetTimerValWithStart(
      kMetricId, kEncodingId, kTimerId, kStartTimestamp, kTimeoutSec,
      &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(TimerManager::isReady(timer_val_ptr));

  status = timer_manager_->GetTimerValWithEnd(kTimerId, kEndTimestamp,
                                              kTimeoutSec, &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(TimerManager::isReady(timer_val_ptr));
}

TEST_F(TimerManagerTests, GetValidMultipartTimer) {
  std::unique_ptr<TimerVal> timer_val_ptr;

  auto status = timer_manager_->GetTimerValWithStart(
      kMetricId, kEncodingId, kTimerId, kStartTimestamp, kTimeoutSec,
      &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(TimerManager::isReady(timer_val_ptr));

  fidl::VectorPtr<fuchsia::cobalt::ObservationValue> parts(1);
  parts->at(0).name = "test_part";
  parts->at(0).encoding_id = kEncodingId;
  parts->at(0).value.set_string_value("test_value");

  status = timer_manager_->GetTimerValWithEnd(kTimerId, kEndTimestamp,
                                              kTimeoutSec, "test_timer",
                                              std::move(parts), &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(TimerManager::isReady(timer_val_ptr));
}

TEST_F(TimerManagerTests, GetValidTimerReverseOrder) {
  std::unique_ptr<TimerVal> timer_val_ptr;

  auto status = timer_manager_->GetTimerValWithEnd(kTimerId, kEndTimestamp,
                                                   kTimeoutSec, &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(TimerManager::isReady(timer_val_ptr));

  status = timer_manager_->GetTimerValWithStart(kMetricId, kEncodingId,
                                                kTimerId, kStartTimestamp,
                                                kTimeoutSec, &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(TimerManager::isReady(timer_val_ptr));
}

TEST_F(TimerManagerTests, TwoStartTimers) {
  std::unique_ptr<TimerVal> timer_val_ptr;

  auto status = timer_manager_->GetTimerValWithStart(
      kMetricId, kEncodingId, kTimerId, kStartTimestamp, kTimeoutSec,
      &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(TimerManager::isReady(timer_val_ptr));

  status = timer_manager_->GetTimerValWithStart(kMetricId, kEncodingId,
                                                kTimerId, kStartTimestamp,
                                                kTimeoutSec, &timer_val_ptr);
  EXPECT_EQ(Status::FAILED_PRECONDITION, status);
}

TEST_F(TimerManagerTests, TwoEndTimers) {
  std::unique_ptr<TimerVal> timer_val_ptr;

  auto status = timer_manager_->GetTimerValWithEnd(kTimerId, kEndTimestamp,
                                                   kTimeoutSec, &timer_val_ptr);

  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(TimerManager::isReady(timer_val_ptr));

  status = timer_manager_->GetTimerValWithEnd(kTimerId, kEndTimestamp,
                                              kTimeoutSec, &timer_val_ptr);
  EXPECT_EQ(Status::FAILED_PRECONDITION, status);
}

TEST_F(TimerManagerTests, NewStartTimerAfterExpiredStartTimer) {
  std::unique_ptr<TimerVal> timer_val_ptr;

  auto status = timer_manager_->GetTimerValWithStart(
      kMetricId, kEncodingId, kTimerId, kStartTimestamp, kTimeoutSec,
      &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(TimerManager::isReady(timer_val_ptr));

  SetTimeSec(10);  // Previous Start expires at time 2s.

  status = timer_manager_->GetTimerValWithStart(kMetricId, kEncodingId,
                                                kTimerId, kStartTimestamp,
                                                kTimeoutSec, &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(TimerManager::isReady(timer_val_ptr));
}

TEST_F(TimerManagerTests, NewEndTimerAfterExpiredEndTimer) {
  std::unique_ptr<TimerVal> timer_val_ptr;

  auto status = timer_manager_->GetTimerValWithEnd(kTimerId, kEndTimestamp,
                                                   kTimeoutSec, &timer_val_ptr);

  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(TimerManager::isReady(timer_val_ptr));

  SetTimeSec(10);  // Previous Start expires at time 2s.

  status = timer_manager_->GetTimerValWithEnd(kTimerId, kEndTimestamp,
                                              kTimeoutSec, &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(TimerManager::isReady(timer_val_ptr));
}

TEST_F(TimerManagerTests, ExpireStartThenGetValidTimer) {
  std::unique_ptr<TimerVal> timer_val_ptr;

  auto status = timer_manager_->GetTimerValWithStart(
      kMetricId, kEncodingId, kTimerId, kStartTimestamp, kTimeoutSec,
      &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(TimerManager::isReady(timer_val_ptr));

  SetTimeSec(10);  // Previous Start expires at time 2s.

  status = timer_manager_->GetTimerValWithStart(kMetricId, kEncodingId,
                                                kTimerId, kStartTimestamp,
                                                kTimeoutSec, &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(TimerManager::isReady(timer_val_ptr));

  status = timer_manager_->GetTimerValWithEnd(kTimerId, kEndTimestamp,
                                              kTimeoutSec, &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(TimerManager::isReady(timer_val_ptr));
}

TEST_F(TimerManagerTests, ExpireStartAddEnd) {
  std::unique_ptr<TimerVal> timer_val_ptr;

  auto status = timer_manager_->GetTimerValWithStart(
      kMetricId, kEncodingId, kTimerId, kStartTimestamp, kTimeoutSec,
      &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(TimerManager::isReady(timer_val_ptr));

  EXPECT_TRUE(RunLoopFor(zx::sec(10)));  // expiry task executed.
  SetTimeSec(10);

  status = timer_manager_->GetTimerValWithEnd(kTimerId, kEndTimestamp,
                                              kTimeoutSec, &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(TimerManager::isReady(timer_val_ptr));

  status = timer_manager_->GetTimerValWithStart(kMetricId, kEncodingId,
                                                kTimerId, kStartTimestamp,
                                                kTimeoutSec, &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(TimerManager::isReady(timer_val_ptr));
}

TEST_F(TimerManagerTests, ExpireStartAddStart) {
  std::unique_ptr<TimerVal> timer_val_ptr;

  auto status = timer_manager_->GetTimerValWithStart(
      kMetricId, kEncodingId, kTimerId, kStartTimestamp, kTimeoutSec,
      &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(TimerManager::isReady(timer_val_ptr));

  EXPECT_TRUE(RunLoopFor(zx::sec(10)));  // expiry task executed.
  SetTimeSec(10);

  status = timer_manager_->GetTimerValWithStart(kMetricId, kEncodingId,
                                                kTimerId, kStartTimestamp,
                                                kTimeoutSec, &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(TimerManager::isReady(timer_val_ptr));

  status = timer_manager_->GetTimerValWithEnd(kTimerId, kEndTimestamp,
                                              kTimeoutSec, &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(TimerManager::isReady(timer_val_ptr));
}

TEST_F(TimerManagerTests, RetrutnValidTimerCancelExpirationTask) {
  std::unique_ptr<TimerVal> timer_val_ptr;

  auto status = timer_manager_->GetTimerValWithStart(
      kMetricId, kEncodingId, kTimerId, kStartTimestamp, 2 * kTimeoutSec,
      &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(TimerManager::isReady(timer_val_ptr));

  SetTimeSec(2);  // Previous Start expires at time 3s.

  status = timer_manager_->GetTimerValWithEnd(kTimerId, kEndTimestamp,
                                              kTimeoutSec, &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(TimerManager::isReady(timer_val_ptr));

  EXPECT_FALSE(RunLoopFor(zx::sec(10)));  // expiry task did not execute.
}

TEST_F(TimerManagerTests, TwoStartTimersFirstExpiryIsCancelled) {
  std::unique_ptr<TimerVal> timer_val_ptr;

  auto status = timer_manager_->GetTimerValWithStart(
      kMetricId, kEncodingId, kTimerId, kStartTimestamp, 2 * kTimeoutSec,
      &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(TimerManager::isReady(timer_val_ptr));

  SetTimeSec(2);  // Previous Start expires at time 3s.

  status = timer_manager_->GetTimerValWithStart(kMetricId, kEncodingId,
                                                kTimerId, kStartTimestamp,
                                                kTimeoutSec, &timer_val_ptr);
  EXPECT_EQ(Status::FAILED_PRECONDITION, status);

  EXPECT_FALSE(RunLoopFor(zx::sec(10)));  // expiry task did not execute.
}

TEST_F(TimerManagerTests, GetTimerValMakeSureExpiryIsCancelled) {
  std::unique_ptr<TimerVal> timer_val_ptr;

  auto status = timer_manager_->GetTimerValWithStart(
      kMetricId, kEncodingId, kTimerId, kStartTimestamp, 2 * kTimeoutSec,
      &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(TimerManager::isReady(timer_val_ptr));

  SetTimeSec(2);  // Previous Start expires at time 3s.

  status = timer_manager_->GetTimerValWithEnd(kTimerId, kEndTimestamp,
                                              kTimeoutSec, &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(TimerManager::isReady(timer_val_ptr));

  EXPECT_FALSE(RunLoopFor(zx::sec(10)));  // expiry task did not execute.
  SetTimeSec(12);

  status = timer_manager_->GetTimerValWithStart(
      kMetricId, kEncodingId, kTimerId, kStartTimestamp, 2 * kTimeoutSec,
      &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_FALSE(TimerManager::isReady(timer_val_ptr));

  SetTimeSec(13);  // Previous Start expires at time 14s.

  status = timer_manager_->GetTimerValWithEnd(kTimerId, kEndTimestamp,
                                              kTimeoutSec, &timer_val_ptr);
  EXPECT_EQ(Status::OK, status);
  EXPECT_TRUE(TimerManager::isReady(timer_val_ptr));
}
}  // namespace cobalt

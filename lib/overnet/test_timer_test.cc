// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test_timer.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Property;
using testing::StrictMock;

namespace overnet {
namespace test_timer_test {

class MockCallback {
 public:
  MOCK_METHOD1(Done, void(const Status&));

  StatusCallback New() {
    return StatusCallback(ALLOCATED_CALLBACK,
                          [this](const Status& status) { this->Done(status); });
  }
};

TEST(TestTimer, CanStep) {
  TestTimer t;
  int64_t start = t.Now().after_epoch().as_us();
  t.Step(10);
  EXPECT_EQ(start + 10, t.Now().after_epoch().as_us());
  t.Step(11);
  EXPECT_EQ(start + 21, t.Now().after_epoch().as_us());
}

TEST(TestTimer, TriggerPastTimeout) {
  StrictMock<MockCallback> cb;

  TestTimer t;
  TimeStamp start = t.Now();
  t.Step(10);

  EXPECT_CALL(cb, Done(Property(&Status::is_ok, true)));
  Timeout timeout(&t, start, cb.New());
}

TEST(TestTimer, TriggerNowTimeout) {
  StrictMock<MockCallback> cb;

  TestTimer t;
  TimeStamp start = t.Now();

  EXPECT_CALL(cb, Done(Property(&Status::is_ok, true)));
  Timeout timeout(&t, start, cb.New());
}

TEST(TestTimer, TriggerFutureTimeout) {
  StrictMock<MockCallback> cb;

  TestTimer t;
  TimeStamp start = t.Now();

  Timeout timeout(&t, start + TimeDelta::FromMicroseconds(100), cb.New());

  // should not schedule yet
  t.Step(50);

  // should schedule now
  EXPECT_CALL(cb, Done(Property(&Status::is_ok, true)));
  t.Step(50);
}

TEST(TestTimer, TriggerFutureTimeout2) {
  StrictMock<MockCallback> cb;

  TestTimer t;
  TimeStamp start = t.Now();

  Timeout timeout(&t, start + TimeDelta::FromMicroseconds(100), cb.New());

  EXPECT_CALL(cb, Done(Property(&Status::is_ok, true)));
  t.Step(10000000);
}

TEST(TestTimer, CancelFutureTimeout) {
  StrictMock<MockCallback> cb;

  TestTimer t;
  TimeStamp start = t.Now();

  Timeout timeout(&t, start + TimeDelta::FromMicroseconds(100), cb.New());

  EXPECT_CALL(cb, Done(Property(&Status::code, StatusCode::CANCELLED)));
  timeout.Cancel();
  // can cancel repeatedly safely
  timeout.Cancel();
}

TEST(TestTimer, CancelFiredTimeout) {
  StrictMock<MockCallback> cb;

  TestTimer t;
  TimeStamp start = t.Now();

  EXPECT_CALL(cb, Done(Property(&Status::is_ok, true)));
  Timeout timeout(&t, start, cb.New());

  // can cancel after execution
  timeout.Cancel();
}

}  // namespace test_timer_test
}  // namespace overnet

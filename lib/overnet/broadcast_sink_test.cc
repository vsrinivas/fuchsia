// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "broadcast_sink.h"
#include <memory>
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InvokeArgument;
using testing::Mock;
using testing::Pointee;
using testing::Property;
using testing::SaveArg;
using testing::StrictMock;

namespace overnet {
namespace broadcast_sink_test {

class MockSink : public Sink<int> {
 public:
  MOCK_METHOD1(Close, void(const Status&));
  MOCK_METHOD2(Pushed, void(int item, std::function<void(const Status&)> done));
  // since gmock has a hard time with move-only types, we provide this override
  // directly, and use Pushed as the mock method (which takes a function that
  // wraps done)
  void Push(int item, StatusCallback done) override {
    auto done_ptr = std::make_shared<StatusCallback>(std::move(done));
    this->Pushed(item,
                 [done_ptr](const Status& status) { (*done_ptr)(status); });
  }
};

class MockSinkCB {
 public:
  MOCK_METHOD1(Callback, void(const StatusOr<Sink<int>*>&));

  StatusOrCallback<Sink<int>*> MakeCallback() {
    return StatusOrCallback<Sink<int>*>(
        [this](const StatusOr<Sink<int>*>& status) { this->Callback(status); });
  }
};

class MockStatusCB {
 public:
  MOCK_METHOD1(Callback, void(const Status&));

  StatusCallback MakeCallback() {
    return StatusCallback(
        [this](const Status& status) { this->Callback(status); });
  }
};

///////////////////////////////////////////////////////////////////////////////
// UnaryBroadcast: adding a BroadcastSink in front of any other sink should
//                 not affect call orderings

TEST(UnaryBroadcast, ImmediateClose) {
  StrictMock<MockSinkCB> start_cb;
  StrictMock<MockSink> mock_sink_1;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&start_cb));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink_1));
  };

  auto* sink = new BroadcastSink<int>(start_cb.MakeCallback());

  auto tgt1 = sink->AddTarget();

  EXPECT_CALL(start_cb,
              Callback(Property(&StatusOr<Sink<int>*>::get, Pointee(sink))));
  tgt1(&mock_sink_1);
  expect_all_done();

  EXPECT_CALL(mock_sink_1, Close(Property(&Status::is_ok, true)));
  sink->Close(Status::Ok());
}

TEST(UnaryBroadcast, PushPushPush) {
  StrictMock<MockSinkCB> start_cb;
  StrictMock<MockStatusCB> status_cb;
  StrictMock<MockSink> mock_sink_1;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&start_cb));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&status_cb));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink_1));
  };

  auto* sink = new BroadcastSink<int>(start_cb.MakeCallback());

  auto tgt1 = sink->AddTarget();

  EXPECT_CALL(start_cb,
              Callback(Property(&StatusOr<Sink<int>*>::get, Pointee(sink))));
  tgt1(&mock_sink_1);
  expect_all_done();

  EXPECT_CALL(mock_sink_1, Pushed(1, _))
      .WillOnce(InvokeArgument<1>(Status::Ok()));
  EXPECT_CALL(status_cb, Callback(Property(&Status::is_ok, true)));
  sink->Push(1, status_cb.MakeCallback());
  expect_all_done();

  EXPECT_CALL(mock_sink_1, Pushed(2, _))
      .WillOnce(InvokeArgument<1>(Status::Ok()));
  EXPECT_CALL(status_cb, Callback(Property(&Status::is_ok, true)));
  sink->Push(2, status_cb.MakeCallback());
  expect_all_done();

  EXPECT_CALL(mock_sink_1, Pushed(3, _))
      .WillOnce(InvokeArgument<1>(Status::Ok()));
  EXPECT_CALL(status_cb, Callback(Property(&Status::is_ok, true)));
  sink->Push(3, status_cb.MakeCallback());
  expect_all_done();

  EXPECT_CALL(mock_sink_1, Close(Property(&Status::is_ok, true)));
  sink->Close(Status::Ok());
}

TEST(UnaryBroadcast, PushFails) {
  StrictMock<MockSinkCB> start_cb;
  StrictMock<MockStatusCB> status_cb;
  StrictMock<MockSink> mock_sink_1;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&start_cb));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&status_cb));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink_1));
  };

  auto* sink = new BroadcastSink<int>(start_cb.MakeCallback());

  auto tgt1 = sink->AddTarget();

  EXPECT_CALL(start_cb,
              Callback(Property(&StatusOr<Sink<int>*>::get, Pointee(sink))));
  tgt1(&mock_sink_1);
  expect_all_done();

  EXPECT_CALL(mock_sink_1, Pushed(1, _))
      .WillOnce(InvokeArgument<1>(Status::Cancelled()));
  EXPECT_CALL(status_cb,
              Callback(Property(&Status::code, StatusCode::CANCELLED)));
  EXPECT_CALL(mock_sink_1,
              Close(Property(&Status::code, StatusCode::CANCELLED)));
  sink->Push(1, status_cb.MakeCallback());
  expect_all_done();

  sink->Close(Status::Ok());
}

TEST(UnaryBroadcast, CloseDuringPush) {
  StrictMock<MockSinkCB> start_cb;
  StrictMock<MockStatusCB> status_cb;
  StrictMock<MockSink> mock_sink_1;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&start_cb));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&status_cb));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink_1));
  };

  auto* sink = new BroadcastSink<int>(start_cb.MakeCallback());

  auto tgt1 = sink->AddTarget();

  EXPECT_CALL(start_cb,
              Callback(Property(&StatusOr<Sink<int>*>::get, Pointee(sink))));
  tgt1(&mock_sink_1);
  expect_all_done();

  std::function<void(const Status&)> push_done;
  EXPECT_CALL(mock_sink_1, Pushed(1, _)).WillOnce(SaveArg<1>(&push_done));
  sink->Push(1, status_cb.MakeCallback());
  expect_all_done();

  EXPECT_CALL(mock_sink_1, Close(Property(&Status::is_ok, true)));
  sink->Close(Status::Ok());

  EXPECT_CALL(status_cb, Callback(Property(&Status::is_ok, true)));
  push_done(Status::Ok());
}

///////////////////////////////////////////////////////////////////////////////
// BinaryBroadcast: things start getting interesting with multiple target sinks
//                  that can fail

TEST(BinaryBroadcast, ImmediateClose) {
  StrictMock<MockSinkCB> start_cb;
  StrictMock<MockSink> mock_sink_1;
  StrictMock<MockSink> mock_sink_2;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&start_cb));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink_1));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink_2));
  };

  auto* sink = new BroadcastSink<int>(start_cb.MakeCallback());

  auto tgt1 = sink->AddTarget();
  auto tgt2 = sink->AddTarget();

  tgt1(&mock_sink_1);
  EXPECT_CALL(start_cb,
              Callback(Property(&StatusOr<Sink<int>*>::get, Pointee(sink))));
  tgt2(&mock_sink_2);
  expect_all_done();

  EXPECT_CALL(mock_sink_1, Close(Property(&Status::is_ok, true)));
  EXPECT_CALL(mock_sink_2, Close(Property(&Status::is_ok, true)));
  sink->Close(Status::Ok());
}

TEST(BinaryBroadcast, PushPushPush) {
  StrictMock<MockSinkCB> start_cb;
  StrictMock<MockStatusCB> status_cb;
  StrictMock<MockSink> mock_sink_1;
  StrictMock<MockSink> mock_sink_2;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&start_cb));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&status_cb));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink_1));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink_2));
  };

  auto* sink = new BroadcastSink<int>(start_cb.MakeCallback());

  auto tgt1 = sink->AddTarget();
  auto tgt2 = sink->AddTarget();

  tgt1(&mock_sink_1);
  EXPECT_CALL(start_cb,
              Callback(Property(&StatusOr<Sink<int>*>::get, Pointee(sink))));
  tgt2(&mock_sink_2);
  expect_all_done();

  EXPECT_CALL(mock_sink_1, Pushed(1, _))
      .WillOnce(InvokeArgument<1>(Status::Ok()));
  EXPECT_CALL(mock_sink_2, Pushed(1, _))
      .WillOnce(InvokeArgument<1>(Status::Ok()));
  EXPECT_CALL(status_cb, Callback(Property(&Status::is_ok, true)));
  sink->Push(1, status_cb.MakeCallback());
  expect_all_done();

  EXPECT_CALL(mock_sink_1, Pushed(2, _))
      .WillOnce(InvokeArgument<1>(Status::Ok()));
  EXPECT_CALL(mock_sink_2, Pushed(2, _))
      .WillOnce(InvokeArgument<1>(Status::Ok()));
  EXPECT_CALL(status_cb, Callback(Property(&Status::is_ok, true)));
  sink->Push(2, status_cb.MakeCallback());
  expect_all_done();

  EXPECT_CALL(mock_sink_1, Pushed(3, _))
      .WillOnce(InvokeArgument<1>(Status::Ok()));
  EXPECT_CALL(mock_sink_2, Pushed(3, _))
      .WillOnce(InvokeArgument<1>(Status::Ok()));
  EXPECT_CALL(status_cb, Callback(Property(&Status::is_ok, true)));
  sink->Push(3, status_cb.MakeCallback());
  expect_all_done();

  EXPECT_CALL(mock_sink_1, Close(Property(&Status::is_ok, true)));
  EXPECT_CALL(mock_sink_2, Close(Property(&Status::is_ok, true)));
  sink->Close(Status::Ok());
}

TEST(BinaryBroadcast, FailFirstDuringSetup) {
  StrictMock<MockSinkCB> start_cb;
  StrictMock<MockSink> mock_sink_1;
  StrictMock<MockSink> mock_sink_2;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&start_cb));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink_1));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink_2));
  };

  auto* sink = new BroadcastSink<int>(start_cb.MakeCallback());

  auto tgt1 = sink->AddTarget();
  auto tgt2 = sink->AddTarget();

  EXPECT_CALL(start_cb, Callback(Property(&StatusOr<Sink<int>*>::code,
                                          StatusCode::CANCELLED)));
  tgt1(Status::Cancelled());
  EXPECT_CALL(mock_sink_2,
              Close(Property(&Status::code, StatusCode::CANCELLED)));
  tgt2(&mock_sink_2);
  expect_all_done();

  sink->Close(Status::Ok());
}

TEST(BinaryBroadcast, FailSecondDuringSetup) {
  StrictMock<MockSinkCB> start_cb;
  StrictMock<MockSink> mock_sink_1;
  StrictMock<MockSink> mock_sink_2;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&start_cb));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink_1));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink_2));
  };

  auto* sink = new BroadcastSink<int>(start_cb.MakeCallback());

  auto tgt1 = sink->AddTarget();
  auto tgt2 = sink->AddTarget();

  tgt1(&mock_sink_1);
  EXPECT_CALL(start_cb, Callback(Property(&StatusOr<Sink<int>*>::code,
                                          StatusCode::CANCELLED)));
  EXPECT_CALL(mock_sink_1,
              Close(Property(&Status::code, StatusCode::CANCELLED)));
  tgt2(Status::Cancelled());
  expect_all_done();

  sink->Close(Status::Ok());
}

TEST(BinaryBroadcast, PushHalfFails1) {
  StrictMock<MockSinkCB> start_cb;
  StrictMock<MockStatusCB> status_cb;
  StrictMock<MockSink> mock_sink_1;
  StrictMock<MockSink> mock_sink_2;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&start_cb));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&status_cb));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink_1));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink_2));
  };

  auto* sink = new BroadcastSink<int>(start_cb.MakeCallback());

  auto tgt1 = sink->AddTarget();
  auto tgt2 = sink->AddTarget();

  tgt1(&mock_sink_1);
  EXPECT_CALL(start_cb,
              Callback(Property(&StatusOr<Sink<int>*>::get, Pointee(sink))));
  tgt2(&mock_sink_2);
  expect_all_done();

  EXPECT_CALL(mock_sink_1, Pushed(1, _))
      .WillOnce(InvokeArgument<1>(Status::Cancelled()));
  EXPECT_CALL(mock_sink_2, Pushed(1, _))
      .WillOnce(InvokeArgument<1>(Status::Ok()));
  EXPECT_CALL(status_cb,
              Callback(Property(&Status::code, StatusCode::CANCELLED)));
  EXPECT_CALL(mock_sink_1,
              Close(Property(&Status::code, StatusCode::CANCELLED)));
  EXPECT_CALL(mock_sink_2,
              Close(Property(&Status::code, StatusCode::CANCELLED)));
  sink->Push(1, status_cb.MakeCallback());
  expect_all_done();

  sink->Close(Status::Ok());
}

TEST(BinaryBroadcast, PushHalfFails2) {
  StrictMock<MockSinkCB> start_cb;
  StrictMock<MockStatusCB> status_cb;
  StrictMock<MockSink> mock_sink_1;
  StrictMock<MockSink> mock_sink_2;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&start_cb));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&status_cb));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink_1));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink_2));
  };

  auto* sink = new BroadcastSink<int>(start_cb.MakeCallback());

  auto tgt1 = sink->AddTarget();
  auto tgt2 = sink->AddTarget();

  tgt1(&mock_sink_1);
  EXPECT_CALL(start_cb,
              Callback(Property(&StatusOr<Sink<int>*>::get, Pointee(sink))));
  tgt2(&mock_sink_2);
  expect_all_done();

  EXPECT_CALL(mock_sink_1, Pushed(1, _))
      .WillOnce(InvokeArgument<1>(Status::Ok()));
  EXPECT_CALL(mock_sink_2, Pushed(1, _))
      .WillOnce(InvokeArgument<1>(Status::Cancelled()));
  EXPECT_CALL(status_cb,
              Callback(Property(&Status::code, StatusCode::CANCELLED)));
  EXPECT_CALL(mock_sink_1,
              Close(Property(&Status::code, StatusCode::CANCELLED)));
  EXPECT_CALL(mock_sink_2,
              Close(Property(&Status::code, StatusCode::CANCELLED)));
  sink->Push(1, status_cb.MakeCallback());
  expect_all_done();

  sink->Close(Status::Ok());
}

}  // namespace broadcast_sink_test
}  // namespace overnet

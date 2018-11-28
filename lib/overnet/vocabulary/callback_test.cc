// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "callback.h"
#include <memory>
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Property;
using testing::StrictMock;

namespace overnet {
namespace callback_test {

typedef std::shared_ptr<int> BoxedInt;
BoxedInt boxed_int() { return BoxedInt(new int(42)); }

class MockCallbackClass {
 public:
  MOCK_METHOD0(Ref, void());
  MOCK_METHOD0(Unref, void());
  MOCK_METHOD1(CallbackStatus, void(const Status&));
  MOCK_METHOD1(CallbackStatusOr, void(const StatusOr<BoxedInt>&));
};

TEST(Callback, CallingEmptyCallbackCrashes) {
  StatusCallback empty;
  EXPECT_DEATH_IF_SUPPORTED(empty(Status::Ok()), "");
}

//////////////////////////////////////////////////////////////////////////////
// StatusCallback

TEST(StatusCallback, NotCalledCallsCancel) {
  StrictMock<MockCallbackClass> mock;

  StatusCallback cb(
      [&mock](const Status& status) { mock.CallbackStatus(status); });

  EXPECT_CALL(mock,
              CallbackStatus(Property(&Status::code, StatusCode::CANCELLED)));
}

TEST(StatusCallback, CalledWorks) {
  StrictMock<MockCallbackClass> mock;

  StatusCallback cb(
      [&mock](const Status& status) { mock.CallbackStatus(status); });

  EXPECT_CALL(mock, CallbackStatus(Property(&Status::is_ok, true)));
  cb(Status::Ok());
  EXPECT_DEATH_IF_SUPPORTED(cb(Status::Ok()), "");
}

TEST(StatusCallback, MoveWorks) {
  StrictMock<MockCallbackClass> mock;

  StatusCallback cb(
      [&mock](const Status& status) { mock.CallbackStatus(status); });

  StatusCallback cb2 = std::move(cb);
  EXPECT_DEATH_IF_SUPPORTED(cb(Status::Ok()), "");

  EXPECT_CALL(mock, CallbackStatus(Property(&Status::is_ok, true)));
  cb2(Status::Ok());
}

TEST(StatusCallback, MoveAssignWorks) {
  StrictMock<MockCallbackClass> mock;

  StatusCallback cb(
      [&mock](const Status& status) { mock.CallbackStatus(status); });

  StatusCallback cb2 = std::move(cb);
  EXPECT_DEATH_IF_SUPPORTED(cb(Status::Ok()), "");

  cb = std::move(cb2);
  EXPECT_DEATH_IF_SUPPORTED(cb2(Status::Ok()), "");

  EXPECT_CALL(mock, CallbackStatus(Property(&Status::is_ok, true)));
  cb(Status::Ok());
}

//////////////////////////////////////////////////////////////////////////////
// StatusOrCallback

TEST(StatusOrCallback, NotCalledCallsCancel) {
  StrictMock<MockCallbackClass> mock;

  StatusOrCallback<BoxedInt> cb([&mock](const StatusOr<BoxedInt>& status) {
    mock.CallbackStatusOr(status);
  });

  EXPECT_CALL(mock, CallbackStatusOr(Property(&StatusOr<BoxedInt>::code,
                                              StatusCode::CANCELLED)));
}

TEST(StatusOrCallback, CalledWorks) {
  StrictMock<MockCallbackClass> mock;

  StatusOrCallback<BoxedInt> cb([&mock](const StatusOr<BoxedInt>& status) {
    mock.CallbackStatusOr(status);
  });

  EXPECT_CALL(mock,
              CallbackStatusOr(Property(&StatusOr<BoxedInt>::is_ok, true)));
  cb(boxed_int());
  EXPECT_DEATH_IF_SUPPORTED(cb(boxed_int()), "");
}

TEST(StatusOrCallback, MoveWorks) {
  StrictMock<MockCallbackClass> mock;

  StatusOrCallback<BoxedInt> cb([&mock](const StatusOr<BoxedInt>& status) {
    mock.CallbackStatusOr(status);
  });

  StatusOrCallback<BoxedInt> cb2 = std::move(cb);
  EXPECT_DEATH_IF_SUPPORTED(cb(boxed_int()), "");

  EXPECT_CALL(mock,
              CallbackStatusOr(Property(&StatusOr<BoxedInt>::is_ok, true)));
  cb2(boxed_int());
}

TEST(StatusOrCallback, MoveAssignWorks) {
  StrictMock<MockCallbackClass> mock;

  StatusOrCallback<BoxedInt> cb([&mock](const StatusOr<BoxedInt>& status) {
    mock.CallbackStatusOr(status);
  });

  StatusOrCallback<BoxedInt> cb2 = std::move(cb);
  EXPECT_DEATH_IF_SUPPORTED(cb(boxed_int()), "");

  cb = std::move(cb2);
  EXPECT_DEATH_IF_SUPPORTED(cb2(boxed_int()), "");

  EXPECT_CALL(mock,
              CallbackStatusOr(Property(&StatusOr<BoxedInt>::is_ok, true)));
  cb(boxed_int());
}

}  // namespace callback_test
}  // namespace overnet

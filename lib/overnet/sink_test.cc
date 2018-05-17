// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sink.h"
#include <functional>
#include <memory>
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeArgument;
using testing::Property;
using testing::StrictMock;

namespace overnet {
namespace sink_test {

class MockCallbacks {
 public:
  MOCK_METHOD1(PushDone, void(const Status&));
  MOCK_METHOD1(PullDone, void(const StatusOr<int>&));

  StatusCallback NewPush() {
    return StatusCallback(ALLOCATED_CALLBACK, [this](const Status& status) {
      this->PushDone(status);
    });
  }

  StatusOrCallback<int> NewPull() {
    return StatusOrCallback<int>(
        ALLOCATED_CALLBACK,
        [this](const StatusOr<int>& status) { this->PullDone(status); });
  }
};

class MockSink : public Sink<int> {
 public:
  MOCK_METHOD2(Closed, void(const Status&, std::function<void()> quiesced));
  MOCK_METHOD2(Pushed, void(int item, std::function<void(const Status&)> done));
  // Since gmock has a hard time with move-only types, we provide this override
  // directly, and use Pushed as the mock method (which takes a function that
  // wraps done).
  void Push(int item, StatusCallback done) override {
    auto done_ptr = std::make_shared<StatusCallback>(std::move(done));
    this->Pushed(item,
                 [done_ptr](const Status& status) { (*done_ptr)(status); });
  }
  void Close(const Status& status, Callback<void> done) override {
    auto done_ptr = std::make_shared<Callback<void>>(std::move(done));
    this->Closed(status, [done_ptr]() { (*done_ptr)(); });
  }
};

class MockSource : public Source<int> {
 public:
  MOCK_METHOD1(Close, void(const Status&));
  MOCK_METHOD1(Pulled,
               void(std::function<void(const StatusOr<Optional<int>>&)> done));
  // Since gmock has a hard time with move-only types, we provide this override
  // directly, and use Pushed as the mock method (which takes a function that
  // wraps done).
  void Pull(StatusOrCallback<Optional<int>> done) override {
    auto done_ptr =
        std::make_shared<StatusOrCallback<Optional<int>>>(std::move(done));
    this->Pulled([done_ptr](const StatusOr<Optional<int>>& status) {
      (*done_ptr)(status);
    });
  }
};

TEST(Sink, PushManyHappy) {
  StrictMock<MockCallbacks> cb;
  StrictMock<MockSink> sink;

  int to_push[] = {1, 2, 3};
  EXPECT_CALL(sink, Pushed(1, _)).WillOnce(InvokeArgument<1>(Status::Ok()));
  EXPECT_CALL(sink, Pushed(2, _)).WillOnce(InvokeArgument<1>(Status::Ok()));
  EXPECT_CALL(sink, Pushed(3, _)).WillOnce(InvokeArgument<1>(Status::Ok()));
  EXPECT_CALL(cb, PushDone(Property(&Status::is_ok, true)));
  sink.PushMany(to_push, sizeof(to_push) / sizeof(*to_push), cb.NewPush());
}

TEST(Sink, PushManyFail) {
  StrictMock<MockCallbacks> cb;
  StrictMock<MockSink> sink;

  int to_push[] = {1, 2, 3};
  EXPECT_CALL(sink, Pushed(1, _)).WillOnce(InvokeArgument<1>(Status::Ok()));
  EXPECT_CALL(sink, Pushed(2, _))
      .WillOnce(InvokeArgument<1>(Status::Cancelled()));
  EXPECT_CALL(cb, PushDone(Property(&Status::code, StatusCode::CANCELLED)));
  sink.PushMany(to_push, sizeof(to_push) / sizeof(*to_push), cb.NewPush());
}

TEST(Source, PullAllHappy) {
  StrictMock<MockCallbacks> cb;
  StrictMock<MockSource> source;

  {
    InSequence in_sequence;
    EXPECT_CALL(source, Pulled(_)).WillOnce(InvokeArgument<0>(1));
    EXPECT_CALL(source, Pulled(_)).WillOnce(InvokeArgument<0>(2));
    EXPECT_CALL(source, Pulled(_)).WillOnce(InvokeArgument<0>(3));
    EXPECT_CALL(source, Pulled(_)).WillOnce(InvokeArgument<0>(Optional<int>()));
  }
  EXPECT_CALL(source, Close(Property(&Status::is_ok, true)));
  source.PullAll([](StatusOr<std::vector<int>> status) {
    ASSERT_TRUE(status.is_ok());
    EXPECT_EQ(*status, (std::vector<int>{1, 2, 3}));
  });
}

TEST(Source, PullAllFail) {
  StrictMock<MockCallbacks> cb;
  StrictMock<MockSource> source;

  {
    InSequence in_sequence;
    EXPECT_CALL(source, Pulled(_)).WillOnce(InvokeArgument<0>(1));
    EXPECT_CALL(source, Pulled(_)).WillOnce(InvokeArgument<0>(2));
    EXPECT_CALL(source, Pulled(_)).WillOnce(InvokeArgument<0>(3));
    EXPECT_CALL(source, Pulled(_))
        .WillOnce(InvokeArgument<0>(Status::Cancelled()));
  }
  EXPECT_CALL(source, Close(Property(&Status::code, StatusCode::CANCELLED)));
  source.PullAll([](StatusOr<std::vector<int>> status) {
    ASSERT_TRUE(status.is_ok());
    EXPECT_EQ(*status, (std::vector<int>{1, 2, 3}));
  });
}

}  // namespace sink_test
}  // namespace overnet

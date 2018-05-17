// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "router.h"
#include <memory>
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Mock;
using testing::Pointee;
using testing::Property;
using testing::SaveArg;
using testing::StrictMock;

namespace overnet {
namespace router_test {

static constexpr TimeStamp kDummyTimestamp123 =
    TimeStamp::AfterEpoch(TimeDelta::FromMicroseconds(123));

typedef std::function<void(const Status&)> StatusFunc;

class MockStreamHandler : public Router::StreamHandler {
 public:
  MOCK_METHOD4(HandleMessageMock,
               void(Optional<SeqNum>, TimeStamp, Slice, StatusFunc));
  // Since gmock has a hard time with move-only types, we provide this override
  // directly, and use HandleMessageMock as the mock method (which takes a
  // function that wraps ready_for_data).
  void HandleMessage(Optional<SeqNum> seq_num, TimeStamp received, Slice data,
                     StatusCallback done) override {
    assert(!done.empty());
    auto cb_ptr = std::make_shared<StatusCallback>(std::move(done));
    auto done_cb = [cb_ptr](const Status& status) { (*cb_ptr)(status); };
    this->HandleMessageMock(seq_num, received, data, done_cb);
  }
};

class MockLink : public Link {
 public:
  MOCK_METHOD1(ForwardMock, void(std::shared_ptr<Message>));
  virtual void Forward(Message message) {
    assert(!message.done.empty());
    ForwardMock(std::make_shared<Message>(std::move(message)));
  }
};

class MockDoneCB {
 public:
  MOCK_METHOD1(Callback, void(const Status&));

  StatusCallback MakeCallback() {
    return [this](const Status& status) { this->Callback(status); };
  }
};

TEST(Router, NoOp) { Router router(NodeId(1)); }

// We should be able to forward messages to ourselves.
TEST(Router, ForwardToSelf) {
  Router router(NodeId(1));

  StrictMock<MockStreamHandler> mock_stream_handler;
  StrictMock<MockDoneCB> done;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_stream_handler));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&done));
  };

  // Establish that there's a stream.
  EXPECT_TRUE(
      router.RegisterStream(NodeId(2), StreamId(1), &mock_stream_handler)
          .is_ok());

  // Forward a message: we should see HandleMessage on the stream.
  StatusFunc done_cb;
  EXPECT_CALL(mock_stream_handler,
              HandleMessageMock(
                  Pointee(Property(&SeqNum::ReconstructFromZero_TestOnly, 1)),
                  kDummyTimestamp123, Slice::FromContainer({1, 2, 3}), _))
      .WillOnce(SaveArg<3>(&done_cb));

  router.Forward(Message{
      std::move(
          RoutableMessage(NodeId(2), false, Slice::FromContainer({1, 2, 3}))
              .AddDestination(NodeId(1), StreamId(1), SeqNum(1, 1))),
      kDummyTimestamp123, done.MakeCallback()});

  expect_all_done();

  // Readying the message for data should propagate back.
  EXPECT_CALL(done, Callback(Property(&Status::is_ok, true)));
  done_cb(Status::Ok());
}

// We should be able to forward messages to ourselves even if the stream isn't
// ready yet.
TEST(Router, ForwardToSelfDelayed) {
  Router router(NodeId(1));

  StrictMock<MockStreamHandler> mock_stream_handler;
  StrictMock<MockDoneCB> done;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_stream_handler));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&done));
  };

  StatusFunc done_cb;

  // Forward a message: nothing should happen.
  router.Forward(Message{
      std::move(
          RoutableMessage(NodeId(2), false, Slice::FromContainer({1, 2, 3}))
              .AddDestination(NodeId(1), StreamId(1), SeqNum(1, 1))),
      kDummyTimestamp123, done.MakeCallback()});

  // Establish that there's a stream: we should see HandleMessage on the stream.
  EXPECT_CALL(mock_stream_handler,
              HandleMessageMock(
                  Pointee(Property(&SeqNum::ReconstructFromZero_TestOnly, 1)),
                  kDummyTimestamp123, Slice::FromContainer({1, 2, 3}), _))
      .WillOnce(SaveArg<3>(&done_cb));
  EXPECT_TRUE(
      router.RegisterStream(NodeId(2), StreamId(1), &mock_stream_handler)
          .is_ok());

  expect_all_done();

  // Readying the message for data should propagate back.
  EXPECT_CALL(done, Callback(Property(&Status::is_ok, true)));
  done_cb(Status::Ok());
}

// We should be able to forward messages to others.
TEST(Router, ForwardToLink) {
  Router router(NodeId(1));

  StrictMock<MockLink> mock_link;
  StrictMock<MockDoneCB> done;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_link));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&done));
  };

  // Establish that there's a link.
  EXPECT_TRUE(router.RegisterLink(NodeId(2), &mock_link).is_ok());

  // Forward a message (we should see it forwarded to the link).
  std::shared_ptr<Message> forwarded_message;
  EXPECT_CALL(mock_link, ForwardMock(_))
      .WillOnce(SaveArg<0>(&forwarded_message));

  router.Forward(Message{
      std::move(
          RoutableMessage(NodeId(1), false, Slice::FromContainer({1, 2, 3}))
              .AddDestination(NodeId(2), StreamId(1), SeqNum(1, 1))),
      kDummyTimestamp123, done.MakeCallback()});

  expect_all_done();

  // Readying the message for data should propagate back.
  EXPECT_CALL(done, Callback(Property(&Status::is_ok, true)));
  forwarded_message->done(Status::Ok());
}

// We should be able to forward messages to others even if the link isn't ready
// yet.
TEST(Router, ForwardToLinkDelayed) {
  Router router(NodeId(1));

  StrictMock<MockLink> mock_link;
  StrictMock<MockDoneCB> done;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_link));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&done));
  };

  std::shared_ptr<Message> forwarded_message;

  // Forward a message: nothing should happen.
  router.Forward(Message{
      std::move(
          RoutableMessage(NodeId(1), false, Slice::FromContainer({1, 2, 3}))
              .AddDestination(NodeId(2), StreamId(1), SeqNum(1, 1))),
      kDummyTimestamp123, done.MakeCallback()});

  // Ready a link: we should see a message forwarded to the link.
  EXPECT_CALL(mock_link, ForwardMock(_))
      .WillOnce(SaveArg<0>(&forwarded_message));
  EXPECT_TRUE(router.RegisterLink(NodeId(2), &mock_link).is_ok());

  expect_all_done();

  // Readying the message for data should propagate back.
  EXPECT_CALL(done, Callback(Property(&Status::is_ok, true)));
  forwarded_message->done(Status::Ok());
}

// We should be able to multicast messages to ourselves and links.
TEST(Router, ForwardToSelfAndLink) {
  Router router(NodeId(1));

  StrictMock<MockStreamHandler> mock_stream_handler;
  StrictMock<MockLink> mock_link;
  StrictMock<MockDoneCB> done;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_stream_handler));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_link));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&done));
  };

  // Ready both link & stream.
  EXPECT_TRUE(
      router.RegisterStream(NodeId(3), StreamId(1), &mock_stream_handler)
          .is_ok());
  EXPECT_TRUE(router.RegisterLink(NodeId(2), &mock_link).is_ok());

  // Forward a message: link and stream should see the message.
  StatusFunc done_cb_stream;
  std::shared_ptr<Message> forwarded_message;
  EXPECT_CALL(mock_stream_handler,
              HandleMessageMock(
                  Pointee(Property(&SeqNum::ReconstructFromZero_TestOnly, 1)),
                  kDummyTimestamp123, Slice::FromContainer({1, 2, 3}), _))
      .WillOnce(SaveArg<3>(&done_cb_stream));
  EXPECT_CALL(mock_link, ForwardMock(_))
      .WillOnce(SaveArg<0>(&forwarded_message));

  router.Forward(Message{
      std::move(
          RoutableMessage(NodeId(3), false, Slice::FromContainer({1, 2, 3}))
              .AddDestination(NodeId(1), StreamId(1), SeqNum(1, 1))
              .AddDestination(NodeId(2), StreamId(1), SeqNum(1, 1))),
      kDummyTimestamp123, done.MakeCallback()});

  expect_all_done();

  // Readying one for data should do nothing.
  done_cb_stream(Status::Ok());

  // Readying the other should back-propagate.
  EXPECT_CALL(done, Callback(Property(&Status::is_ok, true)));

  forwarded_message->done(Status::Ok());
}

// Forwarding a message to two nodes across the same link should multicast.
TEST(Router, ForwardingClumpsStayClumped) {
  Router router(NodeId(1));

  StrictMock<MockLink> mock_link;
  StrictMock<MockDoneCB> done;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_link));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&done));
  };

  // Ready the links.
  EXPECT_TRUE(router.RegisterLink(NodeId(2), &mock_link).is_ok());
  EXPECT_TRUE(router.RegisterLink(NodeId(3), &mock_link).is_ok());

  // Forward a message: should see just one thing going out.
  std::shared_ptr<Message> forwarded_message;
  EXPECT_CALL(mock_link, ForwardMock(_))
      .WillOnce(SaveArg<0>(&forwarded_message));

  router.Forward(Message{
      std::move(
          RoutableMessage(NodeId(1), false, Slice::FromContainer({1, 2, 3}))
              .AddDestination(NodeId(2), StreamId(1), SeqNum(1, 1))
              .AddDestination(NodeId(3), StreamId(1), SeqNum(1, 1))),
      kDummyTimestamp123, done.MakeCallback()});

  expect_all_done();

  // Check the message has the shape we want.
  EXPECT_EQ(2u, forwarded_message->wire.destinations().size());

  // Readying should back-propagate.
  EXPECT_CALL(done, Callback(Property(&Status::is_ok, true)));
  forwarded_message->done(Status::Ok());
}

}  // namespace router_test
}  // namespace overnet

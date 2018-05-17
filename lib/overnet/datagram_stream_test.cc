// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "datagram_stream.h"
#include <memory>
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test_timer.h"

using testing::_;
using testing::Mock;
using testing::Pointee;
using testing::Property;
using testing::SaveArg;
using testing::StrictMock;

namespace overnet {
namespace datagram_stream_tests {

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

class MockPullCB {
 public:
  MOCK_METHOD1(Callback, void(const StatusOr<Optional<Slice>>&));

  StatusOrCallback<Optional<Slice>> MakeCallback() {
    return [this](const StatusOr<Optional<Slice>>& status) {
      this->Callback(status);
    };
  }
};

TEST(DatagramStream, NoOp) {
  TestTimer timer;

  Router router(NodeId(1));

  DatagramStream ds1(&timer, &router, NodeId(2),
                     ReliabilityAndOrdering::ReliableUnordered, StreamId(1));
}

TEST(DatagramStream, UnreliableSend) {
  TestTimer timer;

  StrictMock<MockLink> link;
  StrictMock<MockDoneCB> done_cb;

  Router router(NodeId(1));
  EXPECT_TRUE(router.RegisterLink(NodeId(2), &link).is_ok());

  auto ds1 = std::make_unique<DatagramStream>(
      &timer, &router, NodeId(2), ReliabilityAndOrdering::UnreliableUnordered,
      StreamId(1));

  DatagramStream::SendOp send_op(ds1.get(), 3);
  std::shared_ptr<Message> message;
  EXPECT_CALL(link, ForwardMock(_)).WillOnce(SaveArg<0>(&message));
  // Packet will still be outstanding at destruction
  send_op.Push(Slice::FromContainer({1, 2, 3}), done_cb.MakeCallback());
  Mock::VerifyAndClearExpectations(&link);

  EXPECT_EQ(message->wire.payload(),
            Slice::FromContainer({0, 1, 1, 0, 1, 2, 3}));
  EXPECT_EQ(message->wire.src(), NodeId(1));
  EXPECT_EQ(message->wire.destinations().size(), size_t(1));
  EXPECT_EQ(message->wire.destinations()[0].dst(), NodeId(2));

  EXPECT_CALL(done_cb, Callback(Property(&Status::is_ok, true)));
  message->done(Status::Ok());

  ds1.reset();
}

TEST(DatagramStream, ReadThenRecv) {
  TestTimer timer;

  StrictMock<MockLink> link;
  StrictMock<MockDoneCB> done_cb;
  StrictMock<MockPullCB> pull_cb;

  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&link));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&done_cb));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&pull_cb));
  };

  Router router(NodeId(1));
  EXPECT_TRUE(router.RegisterLink(NodeId(2), &link).is_ok());

  DatagramStream ds1(&timer, &router, NodeId(2),
                     ReliabilityAndOrdering::ReliableUnordered, StreamId(1));

  router.Forward(Message{
      std::move(RoutableMessage(NodeId(2), false,
                                Slice::FromContainer({0, 1, 1, 0, 1, 2, 3}))
                    .AddDestination(NodeId(1), StreamId(1), SeqNum(1, 1))),
      TimeStamp::AfterEpoch(TimeDelta::FromMilliseconds(123)),
      done_cb.MakeCallback()});

  DatagramStream::ReceiveOp recv_op(&ds1);

  EXPECT_CALL(done_cb, Callback(Property(&Status::is_ok, true)));
  EXPECT_CALL(pull_cb, Callback(Property(
                           &StatusOr<Optional<Slice>>::get,
                           Pointee(Pointee(Slice::FromContainer({1, 2, 3}))))));
  recv_op.Pull(pull_cb.MakeCallback());

  expect_all_done();

  recv_op.Close(Status::Ok());
}

TEST(DatagramStream, RecvThenRead) {
  TestTimer timer;

  StrictMock<MockLink> link;
  StrictMock<MockDoneCB> done_cb;
  StrictMock<MockPullCB> pull_cb;

  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&link));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&done_cb));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&pull_cb));
  };

  Router router(NodeId(1));
  EXPECT_TRUE(router.RegisterLink(NodeId(2), &link).is_ok());

  DatagramStream ds1(&timer, &router, NodeId(2),
                     ReliabilityAndOrdering::ReliableUnordered, StreamId(1));

  DatagramStream::ReceiveOp recv_op(&ds1);

  recv_op.Pull(pull_cb.MakeCallback());

  EXPECT_CALL(done_cb, Callback(Property(&Status::is_ok, true)));
  EXPECT_CALL(pull_cb, Callback(Property(
                           &StatusOr<Optional<Slice>>::get,
                           Pointee(Pointee(Slice::FromContainer({1, 2, 3}))))));
  router.Forward(Message{
      std::move(RoutableMessage(NodeId(2), false,
                                Slice::FromContainer({0, 1, 1, 0, 1, 2, 3}))
                    .AddDestination(NodeId(1), StreamId(1), SeqNum(1, 1))),
      TimeStamp::AfterEpoch(TimeDelta::FromMilliseconds(123)),
      done_cb.MakeCallback()});

  expect_all_done();

  recv_op.Close(Status::Ok());
}

}  // namespace datagram_stream_tests
}  // namespace overnet

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

typedef std::function<void(const StatusOr<Sink<Chunk>*>&)> ReadyCallback;

class MockStreamHandler : public Router::StreamHandler {
 public:
  MOCK_METHOD5(HandleMessageMock, void(SeqNum, uint64_t, bool,
                                       ReliabilityAndOrdering, ReadyCallback));
  // Since gmock has a hard time with move-only types, we provide this override
  // directly, and use HandleMessageMock as the mock method (which takes a
  // function that wraps ready_for_data).
  void HandleMessage(SeqNum seq_num, uint64_t payload_length, bool is_control,
                     ReliabilityAndOrdering reliability_and_ordering,
                     StatusOrCallback<Sink<Chunk>*> ready_for_data) override {
    auto ready_cb_ptr = std::make_shared<StatusOrCallback<Sink<Chunk>*>>(
        std::move(ready_for_data));
    auto ready_cb = [ready_cb_ptr](const StatusOr<Sink<Chunk>*>& status) {
      (*ready_cb_ptr)(status);
    };
    this->HandleMessageMock(seq_num, payload_length, is_control,
                            reliability_and_ordering, ready_cb);
  }
};

class MockLink : public Link {
 public:
  MOCK_METHOD1(ForwardMock, void(std::shared_ptr<Message>));
  virtual void Forward(Message message) {
    ForwardMock(std::make_shared<Message>(std::move(message)));
  }
};

class MockSinkCB {
 public:
  MOCK_METHOD1(Callback, void(const StatusOr<Sink<Chunk>*>&));

  StatusOrCallback<Sink<Chunk>*> MakeCallback() {
    return StatusOrCallback<Sink<Chunk>*>(
        [this](const StatusOr<Sink<Chunk>*>& status) {
          this->Callback(status);
        });
  }
};

class MockSink : public Sink<Chunk> {
 public:
  MOCK_METHOD1(Close, void(const Status&));
  MOCK_METHOD2(Pushed, void(const Chunk& item,
                            std::function<void(const Status&)> done));
  // Since gmock has a hard time with move-only types, we provide this override
  // directly, and use Pushed as the mock method (which takes a function that
  // wraps done).
  void Push(Chunk item, StatusCallback done) override {
    auto done_ptr = std::make_shared<StatusCallback>(std::move(done));
    this->Pushed(item,
                 [done_ptr](const Status& status) { (*done_ptr)(status); });
  }
};

TEST(Router, NoOp) { Router router(NodeId(1)); }

// We should be able to forward messages to ourselves.
TEST(Router, ForwardToSelf) {
  Router router(NodeId(1));

  StrictMock<MockStreamHandler> mock_stream_handler;
  StrictMock<MockSinkCB> ready_sink;
  StrictMock<MockSink> mock_sink;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_stream_handler));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&ready_sink));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink));
  };

  // Establish that there's a stream.
  EXPECT_TRUE(
      router.RegisterStream(NodeId(2), StreamId(1), &mock_stream_handler)
          .is_ok());

  // Forward a message: we should see HandleMessage on the stream.
  ReadyCallback ready_cb;
  EXPECT_CALL(
      mock_stream_handler,
      HandleMessageMock(Property(&SeqNum::ReconstructFromZero_TestOnly, 1), 3,
                        false, ReliabilityAndOrdering::ReliableOrdered, _))
      .WillOnce(SaveArg<4>(&ready_cb));

  router.Forward(Message{
      std::move(
          RoutingHeader(NodeId(2), 3, ReliabilityAndOrdering::ReliableOrdered)
              .AddDestination(NodeId(1), StreamId(1), SeqNum(1, 1))),
      ready_sink.MakeCallback()});

  expect_all_done();

  // Readying the message for data should propagate back.
  EXPECT_CALL(ready_sink, Callback(Property(&StatusOr<Sink<Chunk>*>::get,
                                            Pointee(&mock_sink))));
  ready_cb(&mock_sink);
}

// We should be able to forward messages to ourselves even if the stream isn't
// ready yet.
TEST(Router, ForwardToSelfDelayed) {
  Router router(NodeId(1));

  StrictMock<MockStreamHandler> mock_stream_handler;
  StrictMock<MockSinkCB> ready_sink;
  StrictMock<MockSink> mock_sink;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_stream_handler));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&ready_sink));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink));
  };

  ReadyCallback ready_cb;

  // Forward a message: nothing should happen.
  router.Forward(Message{
      std::move(
          RoutingHeader(NodeId(2), 3, ReliabilityAndOrdering::ReliableOrdered)
              .AddDestination(NodeId(1), StreamId(1), SeqNum(1, 1))),
      ready_sink.MakeCallback()});

  // Establish that there's a stream: we should see HandleMessage on the stream.
  EXPECT_CALL(
      mock_stream_handler,
      HandleMessageMock(Property(&SeqNum::ReconstructFromZero_TestOnly, 1), 3,
                        false, ReliabilityAndOrdering::ReliableOrdered, _))
      .WillOnce(SaveArg<4>(&ready_cb));
  EXPECT_TRUE(
      router.RegisterStream(NodeId(2), StreamId(1), &mock_stream_handler)
          .is_ok());

  expect_all_done();

  // Readying the message for data should propagate back.
  EXPECT_CALL(ready_sink, Callback(Property(&StatusOr<Sink<Chunk>*>::get,
                                            Pointee(&mock_sink))));
  ready_cb(&mock_sink);
}

// We should be able to forward messages to others.
TEST(Router, ForwardToLink) {
  Router router(NodeId(1));

  StrictMock<MockLink> mock_link;
  StrictMock<MockSinkCB> ready_sink;
  StrictMock<MockSink> mock_sink;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_link));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&ready_sink));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink));
  };

  // Establish that there's a link.
  EXPECT_TRUE(router.RegisterLink(NodeId(2), &mock_link).is_ok());

  // Forward a message (we should see it forwarded to the link).
  std::shared_ptr<Message> forwarded_message;
  EXPECT_CALL(mock_link, ForwardMock(_))
      .WillOnce(SaveArg<0>(&forwarded_message));

  router.Forward(Message{
      std::move(
          RoutingHeader(NodeId(1), 3, ReliabilityAndOrdering::ReliableOrdered)
              .AddDestination(NodeId(2), StreamId(1), SeqNum(1, 1))),
      ready_sink.MakeCallback()});

  expect_all_done();

  // Readying the message for data should propagate back.
  EXPECT_CALL(ready_sink, Callback(Property(&StatusOr<Sink<Chunk>*>::get,
                                            Pointee(&mock_sink))));
  forwarded_message->ready_for_data(&mock_sink);
}

// We should be able to forward messages to others even if the link isn't ready
// yet.
TEST(Router, ForwardToLinkDelayed) {
  Router router(NodeId(1));

  StrictMock<MockLink> mock_link;
  StrictMock<MockSinkCB> ready_sink;
  StrictMock<MockSink> mock_sink;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_link));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&ready_sink));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink));
  };

  std::shared_ptr<Message> forwarded_message;

  // Forward a message: nothing should happen.
  router.Forward(Message{
      std::move(
          RoutingHeader(NodeId(1), 3, ReliabilityAndOrdering::ReliableOrdered)
              .AddDestination(NodeId(2), StreamId(1), SeqNum(1, 1))),
      ready_sink.MakeCallback()});

  // Ready a link: we should see a message forwarded to the link.
  EXPECT_CALL(mock_link, ForwardMock(_))
      .WillOnce(SaveArg<0>(&forwarded_message));
  EXPECT_TRUE(router.RegisterLink(NodeId(2), &mock_link).is_ok());

  expect_all_done();

  // Readying the message for data should propagate back.
  EXPECT_CALL(ready_sink, Callback(Property(&StatusOr<Sink<Chunk>*>::get,
                                            Pointee(&mock_sink))));
  forwarded_message->ready_for_data(&mock_sink);
}

// We should be able to multicast messages to ourselves and links.
TEST(Router, ForwardToSelfAndLink) {
  Router router(NodeId(1));

  StrictMock<MockStreamHandler> mock_stream_handler;
  StrictMock<MockLink> mock_link;
  StrictMock<MockSinkCB> ready_sink;
  StrictMock<MockSink> mock_sink_from_stream_handler;
  StrictMock<MockSink> mock_sink_from_link;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_stream_handler));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_link));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&ready_sink));
    EXPECT_TRUE(
        Mock::VerifyAndClearExpectations(&mock_sink_from_stream_handler));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink_from_link));
  };

  // Ready both link & stream.
  EXPECT_TRUE(
      router.RegisterStream(NodeId(3), StreamId(1), &mock_stream_handler)
          .is_ok());
  EXPECT_TRUE(router.RegisterLink(NodeId(2), &mock_link).is_ok());

  // Forward a message: link and stream should see the message.
  ReadyCallback ready_cb_stream;
  std::shared_ptr<Message> forwarded_message;
  EXPECT_CALL(
      mock_stream_handler,
      HandleMessageMock(Property(&SeqNum::ReconstructFromZero_TestOnly, 1), 3,
                        false, ReliabilityAndOrdering::ReliableOrdered, _))
      .WillOnce(SaveArg<4>(&ready_cb_stream));
  EXPECT_CALL(mock_link, ForwardMock(_))
      .WillOnce(SaveArg<0>(&forwarded_message));

  router.Forward(Message{
      std::move(
          RoutingHeader(NodeId(3), 3, ReliabilityAndOrdering::ReliableOrdered)
              .AddDestination(NodeId(1), StreamId(1), SeqNum(1, 1))
              .AddDestination(NodeId(2), StreamId(1), SeqNum(1, 1))),
      ready_sink.MakeCallback()});

  expect_all_done();

  // Readying one for data should do nothing.
  ready_cb_stream(&mock_sink_from_stream_handler);

  // Readying the other should back-propagate.
  Sink<Chunk>* broadcast_sink;
  EXPECT_CALL(ready_sink,
              Callback(Property(&StatusOr<Sink<Chunk>*>::is_ok, true)))
      .WillOnce(Invoke([&broadcast_sink](StatusOr<Sink<Chunk>*> s) {
        broadcast_sink = *s.get();
      }));

  forwarded_message->ready_for_data(&mock_sink_from_link);

  expect_all_done();

  // Closing should broadcast (needed to cleanup the internal BroadcastSink).
  EXPECT_CALL(mock_sink_from_stream_handler,
              Close(Property(&Status::is_ok, true)));
  EXPECT_CALL(mock_sink_from_link, Close(Property(&Status::is_ok, true)));
  broadcast_sink->Close(Status::Ok());
}

// Forwarding a message to two nodes across the same link should multicast.
TEST(Router, ForwardingClumpsStayClumped) {
  Router router(NodeId(1));

  StrictMock<MockLink> mock_link;
  StrictMock<MockSinkCB> ready_sink;
  StrictMock<MockSink> mock_sink;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_link));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&ready_sink));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_sink));
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
          RoutingHeader(NodeId(1), 3, ReliabilityAndOrdering::ReliableOrdered)
              .AddDestination(NodeId(2), StreamId(1), SeqNum(1, 1))
              .AddDestination(NodeId(3), StreamId(1), SeqNum(1, 1))),
      ready_sink.MakeCallback()});

  expect_all_done();

  // Check the message has the shape we want.
  EXPECT_EQ(2u, forwarded_message->routing_header.destinations().size());

  // Readying should back-propagate.
  Sink<Chunk>* broadcast_sink;
  EXPECT_CALL(ready_sink,
              Callback(Property(&StatusOr<Sink<Chunk>*>::is_ok, true)))
      .WillOnce(Invoke([&broadcast_sink](StatusOr<Sink<Chunk>*> s) {
        broadcast_sink = *s.get();
      }));
  forwarded_message->ready_for_data(&mock_sink);

  expect_all_done();

  // Closing should broadcast (needed to cleanup the internal BroadcastSink).
  EXPECT_CALL(mock_sink, Close(Property(&Status::is_ok, true)));
  broadcast_sink->Close(Status::Ok());
}

}  // namespace router_test
}  // namespace overnet

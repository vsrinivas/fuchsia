// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "router.h"
#include <memory>
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test_timer.h"

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

class MockLink {
 public:
  MOCK_METHOD1(Forward, void(std::shared_ptr<Message>));

  std::unique_ptr<Link> MakeLink(NodeId src, NodeId peer) {
    class LinkInst final : public Link {
     public:
      LinkInst(MockLink* link, NodeId src, NodeId peer)
          : link_(link),
            fake_link_metrics_(src, peer, 1, reinterpret_cast<uint64_t>(this)) {
      }

      void Forward(Message message) override {
        assert(!message.done.empty());
        link_->Forward(std::make_shared<Message>(std::move(message)));
      }

      LinkMetrics GetLinkMetrics() override { return fake_link_metrics_; }

     private:
      MockLink* link_;
      const LinkMetrics fake_link_metrics_;
    };
    return std::make_unique<LinkInst>(this, src, peer);
  }
};

class MockDoneCB {
 public:
  MOCK_METHOD1(Callback, void(const Status&));

  StatusCallback MakeCallback() {
    return [this](const Status& status) { this->Callback(status); };
  }
};

TEST(Router, NoOp) {
  TestTimer timer;
  Router router(&timer, NodeId(1), true);
}

// We should be able to forward messages to ourselves.
TEST(Router, ForwardToSelf) {
  TestTimer timer;
  Router router(&timer, NodeId(1), true);

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
  TestTimer timer;
  Router router(&timer, NodeId(1), true);

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
  TestTimer timer;
  Router router(&timer, NodeId(1), true);

  StrictMock<MockLink> mock_link;
  StrictMock<MockDoneCB> done;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_link));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&done));
  };

  // Establish that there's a link.
  router.RegisterLink(mock_link.MakeLink(NodeId(1), NodeId(2)));
  while (!router.HasRouteTo(NodeId(2))) {
    router.BlockUntilNoBackgroundUpdatesProcessing();
    timer.StepUntilNextEvent();
  }

  // Forward a message (we should see it forwarded to the link).
  std::shared_ptr<Message> forwarded_message;
  EXPECT_CALL(mock_link, Forward(_)).WillOnce(SaveArg<0>(&forwarded_message));

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
  TestTimer timer;
  Router router(&timer, NodeId(1), true);

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
  EXPECT_CALL(mock_link, Forward(_)).WillOnce(SaveArg<0>(&forwarded_message));
  router.RegisterLink(mock_link.MakeLink(NodeId(1), NodeId(2)));
  while (!router.HasRouteTo(NodeId(2))) {
    router.BlockUntilNoBackgroundUpdatesProcessing();
    timer.StepUntilNextEvent();
  }

  expect_all_done();

  // Readying the message for data should propagate back.
  EXPECT_CALL(done, Callback(Property(&Status::is_ok, true)));
  forwarded_message->done(Status::Ok());
}

// We should be able to multicast messages to ourselves and links.
TEST(Router, ForwardToSelfAndLink) {
  TestTimer timer;
  Router router(&timer, NodeId(1), true);

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
  router.RegisterLink(mock_link.MakeLink(NodeId(1), NodeId(2)));
  while (!router.HasRouteTo(NodeId(2))) {
    router.BlockUntilNoBackgroundUpdatesProcessing();
    timer.StepUntilNextEvent();
  }

  // Forward a message: link and stream should see the message.
  StatusFunc done_cb_stream;
  std::shared_ptr<Message> forwarded_message;
  EXPECT_CALL(mock_stream_handler,
              HandleMessageMock(
                  Pointee(Property(&SeqNum::ReconstructFromZero_TestOnly, 1)),
                  kDummyTimestamp123, Slice::FromContainer({1, 2, 3}), _))
      .WillOnce(SaveArg<3>(&done_cb_stream));
  EXPECT_CALL(mock_link, Forward(_)).WillOnce(SaveArg<0>(&forwarded_message));

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

// TODO(ctiller): re-enable this test.
// Now that links are owned, the trick of registering the same link for two
// nodes no longer works, and this test will require a complete routing table
// in order to function.
#if 0
// Forwarding a message to two nodes across the same link should multicast.
TEST(Router, ForwardingClumpsStayClumped) {
  TestTimer timer;
  Router router(&timer, NodeId(1));

  StrictMock<MockLink> mock_link;
  StrictMock<MockDoneCB> done;
  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_link));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&done));
  };

  // Ready the links.
  router.RegisterLink(mock_link.MakeLink(NodeId(2)));
  router.RegisterLink(mock_link.MakeLink(NodeId(3)));

  // Forward a message: should see just one thing going out.
  std::shared_ptr<Message> forwarded_message;
  EXPECT_CALL(mock_link, Forward(_)).WillOnce(SaveArg<0>(&forwarded_message));

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
#endif

}  // namespace router_test
}  // namespace overnet

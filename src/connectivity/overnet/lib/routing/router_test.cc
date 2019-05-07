// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/routing/router.h"
#include <memory>
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/connectivity/overnet/lib/environment/trace_cout.h"
#include "src/connectivity/overnet/lib/testing/flags.h"
#include "src/connectivity/overnet/lib/testing/test_timer.h"

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

class MockStreamHandler : public Router::StreamHandler {
 public:
  MOCK_METHOD3(HandleMessage, void(SeqNum, TimeStamp, Slice));
  void RouterClose(Callback<void> quiesced) override {}
};

class MockLink {
 public:
  MOCK_METHOD1(Forward, void(std::shared_ptr<Message>));

  LinkPtr<> MakeLink(NodeId src, NodeId peer) {
    class LinkInst final : public Link {
     public:
      LinkInst(MockLink* link, NodeId src, NodeId peer)
          : link_(link), src_(src), peer_(peer) {}

      void Close(Callback<void> quiesced) override {}

      void Forward(Message message) override {
        link_->Forward(std::make_shared<Message>(std::move(message)));
      }

      fuchsia::overnet::protocol::LinkStatus GetLinkStatus() override {
        return fuchsia::overnet::protocol::LinkStatus{src_.as_fidl(),
                                                      peer_.as_fidl(), 1, 1};
      }

      const LinkStats* GetStats() const override { return nullptr; }

     private:
      MockLink* link_;
      const NodeId src_;
      const NodeId peer_;
    };
    return overnet::MakeLink<LinkInst>(this, src, peer);
  }
};

TEST(Router, NoOp) {
  TestTimer timer;
  TraceCout trace(&timer);
  ScopedRenderer scoped_renderer(&trace);
  ScopedSeverity scoped_severity{FLAGS_verbose ? Severity::DEBUG
                                               : Severity::INFO};
  Router router(&timer, NodeId(1), true);
}

// We should be able to forward messages to ourselves.
TEST(Router, ForwardToSelf) {
  TestTimer timer;
  TraceCout trace(&timer);
  ScopedRenderer scoped_renderer(&trace);
  ScopedSeverity scoped_severity{FLAGS_verbose ? Severity::DEBUG
                                               : Severity::INFO};
  Router router(&timer, NodeId(1), true);

  StrictMock<MockStreamHandler> mock_stream_handler;

  // Establish that there's a stream.
  EXPECT_TRUE(
      router.RegisterStream(NodeId(2), StreamId(1), &mock_stream_handler)
          .is_ok());

  // Forward a message: we should see HandleMessage on the stream.
  EXPECT_CALL(
      mock_stream_handler,
      HandleMessage(Property(&SeqNum::ReconstructFromZero_TestOnly, 1),
                    kDummyTimestamp123, Slice::FromContainer({1, 2, 3})));

  router.Forward(
      Message{std::move(RoutableMessage(NodeId(2)).AddDestination(
                  NodeId(1), StreamId(1), SeqNum(1, 1))),
              ForwardingPayloadFactory(Slice::FromContainer({1, 2, 3})),
              kDummyTimestamp123});
}

// We should be able to forward messages to ourselves even if the stream isn't
// ready yet.
TEST(Router, ForwardToSelfDelayed) {
  TestTimer timer;
  TraceCout trace(&timer);
  ScopedRenderer scoped_renderer(&trace);
  ScopedSeverity scoped_severity{FLAGS_verbose ? Severity::DEBUG
                                               : Severity::INFO};
  Router router(&timer, NodeId(1), true);

  StrictMock<MockStreamHandler> mock_stream_handler;

  // Forward a message: nothing should happen.
  router.Forward(
      Message{std::move(RoutableMessage(NodeId(2)).AddDestination(
                  NodeId(1), StreamId(1), SeqNum(1, 1))),
              ForwardingPayloadFactory(Slice::FromContainer({1, 2, 3})),
              kDummyTimestamp123});

  // Establish that there's a stream: we should see HandleMessage on the stream.
  EXPECT_CALL(
      mock_stream_handler,
      HandleMessage(Property(&SeqNum::ReconstructFromZero_TestOnly, 1),
                    kDummyTimestamp123, Slice::FromContainer({1, 2, 3})));
  EXPECT_TRUE(
      router.RegisterStream(NodeId(2), StreamId(1), &mock_stream_handler)
          .is_ok());
}

// We should be able to forward messages to others.
TEST(Router, ForwardToLink) {
  TestTimer timer;
  TraceCout trace(&timer);
  ScopedRenderer scoped_renderer(&trace);
  ScopedSeverity scoped_severity{FLAGS_verbose ? Severity::DEBUG
                                               : Severity::INFO};
  Router router(&timer, NodeId(1), true);

  StrictMock<MockLink> mock_link;

  // Establish that there's a link.
  router.RegisterLink(mock_link.MakeLink(NodeId(1), NodeId(2)));
  while (!router.HasRouteTo(NodeId(2))) {
    router.BlockUntilNoBackgroundUpdatesProcessing();
    timer.StepUntilNextEvent();
  }

  // Forward a message (we should see it forwarded to the link).
  std::shared_ptr<Message> forwarded_message;
  EXPECT_CALL(mock_link, Forward(_)).WillOnce(SaveArg<0>(&forwarded_message));

  router.Forward(
      Message{std::move(RoutableMessage(NodeId(1)).AddDestination(
                  NodeId(2), StreamId(1), SeqNum(1, 1))),
              ForwardingPayloadFactory(Slice::FromContainer({1, 2, 3})),
              kDummyTimestamp123});
}

// We should be able to forward messages to others even if the link isn't ready
// yet.
TEST(Router, ForwardToLinkDelayed) {
  TestTimer timer;
  TraceCout trace(&timer);
  ScopedRenderer scoped_renderer(&trace);
  ScopedSeverity scoped_severity{FLAGS_verbose ? Severity::DEBUG
                                               : Severity::INFO};
  Router router(&timer, NodeId(1), true);

  StrictMock<MockLink> mock_link;

  std::shared_ptr<Message> forwarded_message;

  // Forward a message: nothing should happen.
  router.Forward(
      Message{std::move(RoutableMessage(NodeId(1)).AddDestination(
                  NodeId(2), StreamId(1), SeqNum(1, 1))),
              ForwardingPayloadFactory(Slice::FromContainer({1, 2, 3})),
              kDummyTimestamp123});

  // Ready a link: we should see a message forwarded to the link.
  EXPECT_CALL(mock_link, Forward(_)).WillOnce(SaveArg<0>(&forwarded_message));
  router.RegisterLink(mock_link.MakeLink(NodeId(1), NodeId(2)));
  while (!router.HasRouteTo(NodeId(2))) {
    router.BlockUntilNoBackgroundUpdatesProcessing();
    timer.StepUntilNextEvent();
  }
}

// We should be able to multicast messages to ourselves and links.
TEST(Router, ForwardToSelfAndLink) {
  TestTimer timer;
  TraceCout trace(&timer);
  ScopedRenderer scoped_renderer(&trace);
  ScopedSeverity scoped_severity{FLAGS_verbose ? Severity::DEBUG
                                               : Severity::INFO};
  Router router(&timer, NodeId(1), true);

  StrictMock<MockStreamHandler> mock_stream_handler;
  StrictMock<MockLink> mock_link;

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
  std::shared_ptr<Message> forwarded_message;
  EXPECT_CALL(
      mock_stream_handler,
      HandleMessage(Property(&SeqNum::ReconstructFromZero_TestOnly, 1),
                    kDummyTimestamp123, Slice::FromContainer({1, 2, 3})));
  EXPECT_CALL(mock_link, Forward(_)).WillOnce(SaveArg<0>(&forwarded_message));

  router.Forward(Message{
      std::move(RoutableMessage(NodeId(3))
                    .AddDestination(NodeId(1), StreamId(1), SeqNum(1, 1))
                    .AddDestination(NodeId(2), StreamId(1), SeqNum(1, 1))),
      ForwardingPayloadFactory(Slice::FromContainer({1, 2, 3})),
      kDummyTimestamp123});
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

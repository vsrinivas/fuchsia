// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "datagram_stream.h"
#include <memory>
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test_timer.h"
#include "trace_cout.h"

using testing::_;
using testing::Mock;
using testing::Pointee;
using testing::Property;
using testing::SaveArg;
using testing::StrictMock;

namespace overnet {
namespace datagram_stream_tests {

class MockLink {
 public:
  MOCK_METHOD1(Forward, void(std::shared_ptr<Message>));

  LinkPtr<> MakeLink(NodeId src, NodeId peer) {
    class LinkInst final : public Link {
     public:
      LinkInst(MockLink* link, NodeId src, NodeId peer)
          : link_(link),
            fake_link_metrics_(src, peer, 1, reinterpret_cast<uint64_t>(this)) {
      }

      void Close(Callback<void> quiesced) override {}

      void Forward(Message message) override {
        link_->Forward(std::make_shared<Message>(std::move(message)));
      }

      LinkMetrics GetLinkMetrics() override { return fake_link_metrics_; }

     private:
      MockLink* link_;
      const LinkMetrics fake_link_metrics_;
    };
    return overnet::MakeLink<LinkInst>(this, src, peer);
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

TEST(DatagramStream, UnreliableSend) {
  StrictMock<MockLink> link;
  StrictMock<MockDoneCB> done_cb;

  TestTimer timer;
  auto trace_sink = TraceCout(&timer);

  auto router = MakeClosedPtr<Router>(&timer, trace_sink, NodeId(1), true);
  router->RegisterLink(link.MakeLink(NodeId(1), NodeId(2)));
  while (!router->HasRouteTo(NodeId(2))) {
    router->BlockUntilNoBackgroundUpdatesProcessing();
    timer.StepUntilNextEvent();
  }

  auto ds1 = MakeClosedPtr<DatagramStream>(
      router.get(), trace_sink, NodeId(2),
      ReliabilityAndOrdering::UnreliableUnordered, StreamId(1));

  auto send_op = MakeClosedPtr<DatagramStream::SendOp>(ds1.get(), 3);
  std::shared_ptr<Message> message;
  EXPECT_CALL(link, Forward(_)).WillOnce(SaveArg<0>(&message));
  // Packet will still be outstanding at destruction.
  send_op->Push(Slice::FromContainer({1, 2, 3}));
  Mock::VerifyAndClearExpectations(&link);

  TimeStamp when = timer.Now();
  EXPECT_EQ(message->make_payload(LazySliceArgs{
                0, std::numeric_limits<uint32_t>::max(), false, &when}),
            Slice::FromContainer({0, 0x80, 1, 0, 1, 2, 3}));
  EXPECT_EQ(message->header.src(), NodeId(1));
  EXPECT_EQ(message->header.destinations().size(), size_t(1));
  EXPECT_EQ(message->header.destinations()[0].dst(), NodeId(2));

  // Stream will send a close.
  EXPECT_CALL(link, Forward(_));
}

TEST(DatagramStream, ReadThenRecv) {
  TestTimer timer;
  auto trace_sink = TraceCout(&timer);

  StrictMock<MockLink> link;
  StrictMock<MockPullCB> pull_cb;

  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&link));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&pull_cb));
  };

  auto router = MakeClosedPtr<Router>(&timer, trace_sink, NodeId(1), true);
  router->RegisterLink(link.MakeLink(NodeId(1), NodeId(2)));
  while (!router->HasRouteTo(NodeId(2))) {
    router->BlockUntilNoBackgroundUpdatesProcessing();
    timer.StepUntilNextEvent();
  }

  auto ds1 = MakeClosedPtr<DatagramStream>(
      router.get(), trace_sink, NodeId(2),
      ReliabilityAndOrdering::ReliableUnordered, StreamId(1));

  router->Forward(Message{
      std::move(RoutableMessage(NodeId(2)).AddDestination(
          NodeId(1), StreamId(1), SeqNum(1, 1))),
      ForwardingPayloadFactory(Slice::FromContainer({0, 0x80, 1, 0, 1, 2, 3})),
      TimeStamp::AfterEpoch(TimeDelta::FromMilliseconds(123))});

  DatagramStream::ReceiveOp recv_op(ds1.get());

  EXPECT_CALL(pull_cb, Callback(Property(
                           &StatusOr<Optional<Slice>>::get,
                           Pointee(Pointee(Slice::FromContainer({1, 2, 3}))))));
  recv_op.Pull(pull_cb.MakeCallback());

  expect_all_done();

  recv_op.Close(Status::Ok());

  // Stream will send a close.
  EXPECT_CALL(link, Forward(_));
}

TEST(DatagramStream, RecvThenRead) {
  TestTimer timer;
  auto trace_sink = TraceCout(&timer);

  StrictMock<MockLink> link;
  StrictMock<MockPullCB> pull_cb;

  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&link));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&pull_cb));
  };

  auto router = MakeClosedPtr<Router>(&timer, trace_sink, NodeId(1), true);
  router->RegisterLink(link.MakeLink(NodeId(1), NodeId(2)));
  while (!router->HasRouteTo(NodeId(2))) {
    router->BlockUntilNoBackgroundUpdatesProcessing();
    timer.StepUntilNextEvent();
  }

  auto ds1 = MakeClosedPtr<DatagramStream>(
      router.get(), trace_sink, NodeId(2),
      ReliabilityAndOrdering::ReliableUnordered, StreamId(1));

  DatagramStream::ReceiveOp recv_op(ds1.get());

  recv_op.Pull(pull_cb.MakeCallback());

  EXPECT_CALL(pull_cb, Callback(Property(
                           &StatusOr<Optional<Slice>>::get,
                           Pointee(Pointee(Slice::FromContainer({1, 2, 3}))))));
  router->Forward(Message{
      std::move(RoutableMessage(NodeId(2)).AddDestination(
          NodeId(1), StreamId(1), SeqNum(1, 1))),
      ForwardingPayloadFactory(Slice::FromContainer({0, 0x80, 1, 0, 1, 2, 3})),
      TimeStamp::AfterEpoch(TimeDelta::FromMilliseconds(123))});

  expect_all_done();

  recv_op.Close(Status::Ok());

  // Stream will send a close.
  EXPECT_CALL(link, Forward(_));
}

}  // namespace datagram_stream_tests
}  // namespace overnet

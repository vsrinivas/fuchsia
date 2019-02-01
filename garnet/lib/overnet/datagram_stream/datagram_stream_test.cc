// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/overnet/datagram_stream/datagram_stream.h"
#include <memory>
#include "garnet/lib/overnet/testing/test_timer.h"
#include "garnet/lib/overnet/testing/trace_cout.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

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
          : link_(link), src_(src), peer_(peer) {}

      void Close(Callback<void> quiesced) override {}

      void Forward(Message message) override {
        link_->Forward(std::make_shared<Message>(std::move(message)));
      }

      fuchsia::overnet::protocol::LinkMetrics GetLinkMetrics() override {
        fuchsia::overnet::protocol::LinkMetrics lm;
        lm.set_label(fuchsia::overnet::protocol::LinkLabel{
            src_.as_fidl(), peer_.as_fidl(), 1, 1});
        return lm;
      }

     private:
      MockLink* link_;
      const NodeId src_;
      const NodeId peer_;
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

// Wrapper that calls Register() automatically (as required by the
// DatagramStream contract).
class DGStream : public DatagramStream {
 public:
  template <class... Arg>
  DGStream(Arg&&... args) : DatagramStream(std::forward<Arg>(args)...) {
    Register();
  }
};

TEST(DatagramStream, UnreliableSend) {
  StrictMock<MockLink> link;
  StrictMock<MockDoneCB> done_cb;

  TestTimer timer;
  TraceCout renderer(&timer);
  ScopedRenderer scoped_render(&renderer);

  auto router = MakeClosedPtr<Router>(&timer, NodeId(1), true);
  router->RegisterLink(link.MakeLink(NodeId(1), NodeId(2)));
  while (!router->HasRouteTo(NodeId(2))) {
    router->BlockUntilNoBackgroundUpdatesProcessing();
    timer.StepUntilNextEvent();
  }

  auto ds1 = MakeClosedPtr<DGStream>(
      router.get(), NodeId(2),
      fuchsia::overnet::protocol::ReliabilityAndOrdering::UnreliableUnordered,
      StreamId(1));

  std::shared_ptr<Message> message;
  EXPECT_CALL(link, Forward(_)).WillOnce(SaveArg<0>(&message));
  // Packet will still be outstanding at destruction.
  DatagramStream::SendOp(ds1.get(), 3)
      .Push(Slice::FromContainer({1, 2, 3}), Callback<void>::Ignored());
  Mock::VerifyAndClearExpectations(&link);

  EXPECT_EQ(message->make_payload(LazySliceArgs{
                Border::None(), std::numeric_limits<uint32_t>::max(), false}),
            Slice::FromContainer({0, 0x80, 1, 0, 1, 2, 3}));
  EXPECT_EQ(message->header.src(), NodeId(1));
  EXPECT_EQ(message->header.destinations().size(), size_t(1));
  EXPECT_EQ(message->header.destinations()[0].dst(), NodeId(2));

  // Stream will send a close.
  EXPECT_CALL(link, Forward(_));
}

TEST(DatagramStream, ReadThenRecv) {
  TestTimer timer;
  TraceCout renderer(&timer);
  ScopedRenderer scoped_render(&renderer);

  MockLink link;
  StrictMock<MockPullCB> pull_cb;

  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&link));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&pull_cb));
  };

  auto router = MakeClosedPtr<Router>(&timer, NodeId(1), true);
  router->RegisterLink(link.MakeLink(NodeId(1), NodeId(2)));
  while (!router->HasRouteTo(NodeId(2))) {
    router->BlockUntilNoBackgroundUpdatesProcessing();
    timer.StepUntilNextEvent();
  }

  auto ds1 = MakeClosedPtr<DGStream>(
      router.get(), NodeId(2),
      fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableUnordered,
      StreamId(1));

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
}

TEST(DatagramStream, RecvThenRead) {
  TestTimer timer;
  TraceCout renderer(&timer);
  ScopedRenderer scoped_render(&renderer);

  MockLink link;
  StrictMock<MockPullCB> pull_cb;

  auto expect_all_done = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&link));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&pull_cb));
  };

  auto router = MakeClosedPtr<Router>(&timer, NodeId(1), true);
  router->RegisterLink(link.MakeLink(NodeId(1), NodeId(2)));
  while (!router->HasRouteTo(NodeId(2))) {
    router->BlockUntilNoBackgroundUpdatesProcessing();
    timer.StepUntilNextEvent();
  }

  auto ds1 = MakeClosedPtr<DGStream>(
      router.get(), NodeId(2),
      fuchsia::overnet::protocol::ReliabilityAndOrdering::ReliableUnordered,
      StreamId(1));

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
}

}  // namespace datagram_stream_tests
}  // namespace overnet

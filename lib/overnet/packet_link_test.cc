// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "packet_link.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test_timer.h"

using testing::_;
using testing::Mock;
using testing::Property;
using testing::SaveArg;
using testing::StrictMock;

namespace overnet {
namespace packet_link_test {

static constexpr uint32_t kTestMSS = 1500;

typedef std::function<void(const Status&)> StatusFunc;

class MockDoneCB {
 public:
  MOCK_METHOD1(Callback, void(const Status&));

  StatusCallback MakeCallback() {
    return [this](const Status& status) { this->Callback(status); };
  }
};

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

class MockPacketLink {
 public:
  MOCK_METHOD1(Emit, void(Slice));

  std::unique_ptr<PacketLink> MakeLink(Router* router, NodeId peer,
                                       uint32_t mss) {
    class PacketLinkImpl final : public PacketLink {
     public:
      PacketLinkImpl(MockPacketLink* mock, Router* router, NodeId peer,
                     uint32_t mss)
          : PacketLink(router, peer, mss), mock_(mock) {}
      void Emit(Slice packet) { mock_->Emit(std::move(packet)); }

     private:
      MockPacketLink* mock_;
    };
    return std::make_unique<PacketLinkImpl>(this, router, peer, mss);
  }
};

TEST(PacketLink, NoOp) {
  TestTimer timer;
  StrictMock<MockPacketLink> mock_link;
  Router router(&timer, NodeId(1), true);
  router.RegisterLink(mock_link.MakeLink(&router, NodeId(2), kTestMSS));
}

TEST(PacketLink, SendOne) {
  TestTimer timer;
  StrictMock<MockPacketLink> mock_link;
  StrictMock<MockDoneCB> done_cb;

  auto verify_all = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_link));
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&done_cb));
  };

  Router router(&timer, NodeId(1), true);
  router.RegisterLink(mock_link.MakeLink(&router, NodeId(2), kTestMSS));
  while (!router.HasRouteTo(NodeId(2))) {
    router.BlockUntilNoBackgroundUpdatesProcessing();
    timer.StepUntilNextEvent();
  }

  Slice emitted;
  EXPECT_CALL(mock_link, Emit(_)).WillOnce(SaveArg<0>(&emitted));
  EXPECT_CALL(done_cb, Callback(Property(&Status::is_ok, true)));
  router.Forward(Message{
      std::move(
          RoutableMessage(NodeId(1), false, Slice::FromContainer({7, 8, 9}))
              .AddDestination(NodeId(2), StreamId(1), SeqNum(1, 1))),
      timer.Now(),
      done_cb.MakeCallback(),
  });
  verify_all();

  EXPECT_EQ(Slice::FromContainer({0, 1, 0, 6, 0, 1, 1, 7, 8, 9}), emitted);
}

TEST(PacketLink, RecvOne) {
  TestTimer timer;
  StrictMock<MockPacketLink> mock_link;
  StrictMock<MockStreamHandler> mock_stream_handler;

  Router router(&timer, NodeId(2), true);
  auto link_unique = mock_link.MakeLink(&router, NodeId(1), kTestMSS);
  auto* link = link_unique.get();
  router.RegisterLink(std::move(link_unique));
  while (!router.HasRouteTo(NodeId(2))) {
    router.BlockUntilNoBackgroundUpdatesProcessing();
    timer.StepUntilNextEvent();
  }
  router.RegisterStream(NodeId(1), StreamId(1), &mock_stream_handler);

  EXPECT_CALL(mock_stream_handler, HandleMessageMock(_, _, _, _));
  link->Process(timer.Now(),
                Slice::FromContainer({0, 1, 0, 6, 0, 1, 1, 7, 8, 9}));
}

}  // namespace packet_link_test
}  // namespace overnet

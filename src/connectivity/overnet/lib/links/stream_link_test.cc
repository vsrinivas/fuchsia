// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/links/stream_link.h"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "src/connectivity/overnet/lib/environment/trace_cout.h"
#include "src/connectivity/overnet/lib/testing/flags.h"
#include "src/connectivity/overnet/lib/testing/test_timer.h"

using testing::_;
using testing::Mock;
using testing::Property;
using testing::SaveArg;
using testing::StrictMock;

namespace overnet {
namespace stream_link_test {

constexpr auto kTestMSS = 2048;

class MockStreamHandler : public Router::StreamHandler {
 public:
  MOCK_METHOD3(HandleMessage, void(SeqNum, TimeStamp, Slice));
  void RouterClose(Callback<void> quiesced) override {}
};

class MockStreamLink {
 public:
  MOCK_METHOD2(Emit, void(Slice, std::function<void(Status)>));

  LinkPtr<StreamLink> MakeLink(Router* router, NodeId peer, uint32_t mss) {
    static uint64_t next_label = 1;
    class PacketLinkImpl final : public StreamLink {
     public:
      PacketLinkImpl(MockStreamLink* mock, Router* router, NodeId peer,
                     uint32_t mss)
          : StreamLink(router, peer, mss, next_label++), mock_(mock) {}
      void Emit(Slice packet, Callback<Status> done) {
        auto cb = std::make_shared<Callback<Status>>(std::move(done));
        mock_->Emit(std::move(packet),
                    [cb](const Status& status) { (*cb)(status); });
      }

     private:
      MockStreamLink* mock_;
    };
    return overnet::MakeLink<PacketLinkImpl>(this, router, peer, mss);
  }
};

class TestEnvironment {
 public:
  TestTimer* timer() { return &timer_; }

 private:
  TestTimer timer_;
  TraceCout renderer_{&timer_};
  ScopedRenderer scoped_renderer{&renderer_};
  ScopedSeverity scoped_severity_{FLAGS_verbose ? Severity::DEBUG
                                                : Severity::INFO};
};

TEST(StreamLink, NoOp) {
  TestEnvironment env;

  StrictMock<MockStreamLink> mock_link;
  Router router(env.timer(), NodeId(1), true);
  router.RegisterLink(mock_link.MakeLink(&router, NodeId(2), kTestMSS));
}

TEST(StreamLink, SendOne) {
  TestEnvironment env;
  StrictMock<MockStreamLink> mock_link;

  auto verify_all = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_link));
  };

  Router router(env.timer(), NodeId(1), true);
  router.RegisterLink(mock_link.MakeLink(&router, NodeId(2), kTestMSS));
  while (!router.HasRouteTo(NodeId(2))) {
    router.BlockUntilNoBackgroundUpdatesProcessing();
    env.timer()->StepUntilNextEvent();
  }

  Slice emitted;
  std::function<void(Status)> done_emit;
  EXPECT_CALL(mock_link, Emit(_, _))
      .WillOnce(DoAll(SaveArg<0>(&emitted), SaveArg<1>(&done_emit)));
  router.Forward(Message{
      std::move(RoutableMessage(NodeId(1)).AddDestination(
          NodeId(2), StreamId(1), SeqNum(1, 1))),
      ForwardingPayloadFactory(Slice::FromContainer({7, 8, 9})),
      env.timer()->Now(),
  });
  verify_all();

  done_emit(Status::Ok());

  EXPECT_EQ(Slice::FromContainer({6, 0, 1, 1, 7, 8, 9}), emitted);
}

TEST(StreamLink, RecvOne) {
  TestEnvironment env;
  StrictMock<MockStreamLink> mock_link;
  StrictMock<MockStreamHandler> mock_stream_handler;

  Router router(env.timer(), NodeId(2), true);
  auto link_unique = mock_link.MakeLink(&router, NodeId(1), kTestMSS);
  auto* link = link_unique.get();
  router.RegisterLink(std::move(link_unique));
  while (!router.HasRouteTo(NodeId(2))) {
    router.BlockUntilNoBackgroundUpdatesProcessing();
    env.timer()->StepUntilNextEvent();
  }
  EXPECT_TRUE(
      router.RegisterStream(NodeId(1), StreamId(1), &mock_stream_handler)
          .is_ok());

  EXPECT_CALL(mock_stream_handler, HandleMessage(_, _, _));
  link->Process(env.timer()->Now(),
                Slice::FromContainer({6, 0, 1, 1, 7, 8, 9}));
}

}  // namespace stream_link_test
}  // namespace overnet

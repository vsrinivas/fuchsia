// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/links/stream_link.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/connectivity/overnet/lib/environment/trace_cout.h"
#include "src/connectivity/overnet/lib/protocol/reliable_framer.h"
#include "src/connectivity/overnet/lib/protocol/unreliable_framer.h"
#include "src/connectivity/overnet/lib/testing/flags.h"
#include "src/connectivity/overnet/lib/testing/test_timer.h"

using testing::_;
using testing::Mock;
using testing::Property;
using testing::SaveArg;
using testing::StrictMock;

namespace overnet {
namespace stream_link_test {

class MockStreamHandler : public Router::StreamHandler {
 public:
  MOCK_METHOD3(HandleMessage, void(SeqNum, TimeStamp, Slice));
  void RouterClose(Callback<void> quiesced) override {}
};

class MockStreamLink {
 public:
  MOCK_METHOD2(Emit, void(Slice, std::function<void(Status)>));

  LinkPtr<StreamLink> MakeLink(Router* router, NodeId peer,
                               std::unique_ptr<StreamFramer> framer) {
    static uint64_t next_label = 1;
    class PacketLinkImpl final : public StreamLink {
     public:
      PacketLinkImpl(MockStreamLink* mock, Router* router, NodeId peer,
                     std::unique_ptr<StreamFramer> framer)
          : StreamLink(router, peer, std::move(framer), next_label++),
            mock_(mock) {}
      void Emit(Slice packet, Callback<Status> done) {
        auto cb = std::make_shared<Callback<Status>>(std::move(done));
        mock_->Emit(std::move(packet),
                    [cb](const Status& status) { (*cb)(status); });
      }

     private:
      MockStreamLink* mock_;
    };
    return overnet::MakeLink<PacketLinkImpl>(this, router, peer,
                                             std::move(framer));
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

struct TestArg {
  std::function<std::unique_ptr<StreamFramer>()> make_framer;
  Slice payload;
  Slice framed;
};

struct StreamLinkTest : public ::testing::TestWithParam<TestArg> {};

TEST_P(StreamLinkTest, SendOne) {
  TestEnvironment env;
  StrictMock<MockStreamLink> mock_link;

  auto verify_all = [&]() {
    EXPECT_TRUE(Mock::VerifyAndClearExpectations(&mock_link));
  };

  Router router(env.timer(), NodeId(1), true);
  router.RegisterLink(
      mock_link.MakeLink(&router, NodeId(2), GetParam().make_framer()));
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
      ForwardingPayloadFactory(Slice::FromContainer(GetParam().payload)),
      env.timer()->Now(),
  });
  verify_all();

  done_emit(Status::Ok());

  EXPECT_EQ(GetParam().framed, emitted);
}

TEST_P(StreamLinkTest, RecvOne) {
  TestEnvironment env;
  StrictMock<MockStreamLink> mock_link;
  StrictMock<MockStreamHandler> mock_stream_handler;

  Router router(env.timer(), NodeId(2), true);
  auto link_unique =
      mock_link.MakeLink(&router, NodeId(1), GetParam().make_framer());
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
  link->Process(env.timer()->Now(), GetParam().framed);
}

INSTANTIATE_TEST_SUITE_P(
    StreamLinkSuite, StreamLinkTest,
    ::testing::Values(
        TestArg{[] { return std::make_unique<ReliableFramer>(); },
                Slice::FromContainer({7, 8, 9}),
                Slice::FromContainer({0x06, 0x00, 6, 0, 1, 1, 7, 8, 9})},
        TestArg{[] { return std::make_unique<UnreliableFramer>(); },
                Slice::FromContainer({7, 8, 9}),
                Slice::FromContainer({0x0a, 6, 6, 0, 1, 1, 7, 8, 9, 0xb8, 0x80,
                                      0x2a, 0xcf})}));

}  // namespace stream_link_test
}  // namespace overnet

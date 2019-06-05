// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/links/packet_nub.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "src/connectivity/overnet/lib/environment/trace_cout.h"
#include "src/connectivity/overnet/lib/testing/flags.h"
#include "src/connectivity/overnet/lib/testing/test_timer.h"

using testing::_;
using testing::InvokeWithoutArgs;
using testing::Mock;
using testing::SaveArg;
using testing::StrictMock;

namespace overnet {
namespace packet_nub_test {

using FakeAddress = uint32_t;

class MockPacketNubBase {
 public:
  MockPacketNubBase(Timer* timer, NodeId node) : router_(timer, node, true) {}

  Router* GetRouter() { return &router_; }

 protected:
  Router router_;
};

class MockPacketNub : public MockPacketNubBase,
                      public PacketNub<FakeAddress, 1024> {
 public:
  MockPacketNub(Timer* timer, NodeId node)
      : MockPacketNubBase(timer, node),
        PacketNub<FakeAddress, 1024>(GetRouter()) {}

  MOCK_METHOD2(SendTo, void(FakeAddress, Slice));
  MOCK_METHOD1(PublishMock, void(std::shared_ptr<LinkPtr<>>));

  void Publish(LinkPtr<> link) override final {
    PublishMock(std::make_shared<LinkPtr<>>(std::move(link)));
  }
};

MATCHER_P(PacketMatches, value, "") {
  if (arg.length() < sizeof(uint64_t)) {
    *result_listener << "the packet is too short: " << arg;
    return false;
  }
  auto tail = arg.FromOffset(sizeof(uint64_t));
  if (tail != value) {
    *result_listener << "the packet tail is: " << tail;
    return false;
  }
  return true;
}

MATCHER_P(PaddedPacketMatches, value, "") {
  if (arg.length() < sizeof(uint64_t)) {
    *result_listener << "the packet is too short for a link id";
    return false;
  }
  auto tail = arg.FromOffset(sizeof(uint64_t));
  if (tail.length() < value.length()) {
    *result_listener << "the packet is too short for the value";
    return false;
  }
  auto expect = Slice::WithInitializer(tail.length(), [=](uint8_t* p) {
    memcpy(p, value.begin(), value.length());
    memset(p + value.length(), 0, tail.length() - value.length());
  });
  if (tail != expect) {
    *result_listener << "the packet tail is: " << tail;
    return false;
  }
  return true;
}

TEST(PacketNub, NoOp) {
  TestTimer timer;
  TraceCout trace(&timer);
  ScopedRenderer scoped_renderer(&trace);
  ScopedSeverity scoped_severity{FLAGS_verbose ? Severity::DEBUG
                                               : Severity::INFO};
  StrictMock<MockPacketNub> nub(&timer, NodeId(1));
}

TEST(PacketNub, InitiateBiggerNodeId) {
  TestTimer timer;
  StrictMock<MockPacketNub> nub(&timer, NodeId(1));

  EXPECT_CALL(nub, SendTo(123, PaddedPacketMatches(Slice::FromContainer(
                                   {2, 1, 0, 0, 0, 0, 0, 0, 0}))));
  nub.Initiate({123}, NodeId(2));

  int remaining = 4;
  EXPECT_CALL(nub, SendTo(123, PaddedPacketMatches(Slice::FromContainer(
                                   {2, 1, 0, 0, 0, 0, 0, 0, 0}))))
      .Times(remaining)
      .WillRepeatedly(InvokeWithoutArgs([&remaining]() { remaining--; }));
  for (int i = 0; i < 100 && remaining > 0; i++) {
    if (!timer.StepUntilNextEvent()) {
      break;
    }
  }
  EXPECT_EQ(remaining, 0);
}

TEST(PacketNub, InitiateSmallerNodeId) {
  TestTimer timer;
  TraceCout trace(&timer);
  ScopedRenderer scoped_renderer(&trace);
  ScopedSeverity scoped_severity{FLAGS_verbose ? Severity::DEBUG
                                               : Severity::INFO};

  StrictMock<MockPacketNub> nub(&timer, NodeId(2));

  EXPECT_CALL(nub, SendTo(123, PaddedPacketMatches(Slice::FromContainer(
                                   {1, 2, 0, 0, 0, 0, 0, 0, 0}))));
  nub.Initiate({123}, NodeId(1));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&nub));

  int remaining = 4;
  EXPECT_CALL(nub, SendTo(123, PaddedPacketMatches(Slice::FromContainer(
                                   {1, 2, 0, 0, 0, 0, 0, 0, 0}))))
      .Times(remaining)
      .WillRepeatedly(InvokeWithoutArgs([&remaining]() { remaining--; }));
  for (int i = 0; i < 100 && remaining > 0; i++) {
    if (!timer.StepUntilNextEvent()) {
      break;
    }
  }
  EXPECT_EQ(remaining, 0);
}

TEST(PacketNub, ProcessHandshakeFromAnnounce) {
  TestTimer timer;
  TraceCout trace(&timer);
  ScopedRenderer scoped_renderer(&trace);
  ScopedSeverity scoped_severity{FLAGS_verbose ? Severity::DEBUG
                                               : Severity::INFO};

  StrictMock<MockPacketNub> nub(&timer, NodeId(1));

  EXPECT_CALL(nub, SendTo(123, PaddedPacketMatches(Slice::FromContainer(
                                   {2, 1, 0, 0, 0, 0, 0, 0, 0}))));
  nub.Process(
      timer.Now(), 123,
      Slice::WithInitializer(MockPacketNub::kCallMeMaybeSize, [](uint8_t* p) {
        // Announce packet with the local node id
        memset(p, 0, MockPacketNub::kCallMeMaybeSize);
        static const uint8_t prefix[] = {1, 2, 3, 4, 5, 6, 7, 8, 1,
                                         2, 0, 0, 0, 0, 0, 0, 0};
        static_assert(sizeof(prefix) <= MockPacketNub::kCallMeMaybeSize,
                      "PacketNub::kHelloSize too small");
        memcpy(p, prefix, sizeof(prefix));
      }));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&nub));

  EXPECT_CALL(nub, SendTo(123, PacketMatches(Slice::FromContainer(
                                   {0}) /* Connected packet */)));
  EXPECT_CALL(nub, PublishMock(_));
  nub.Process(
      timer.Now(), 123,
      Slice::FromContainer({1, 2, 3, 4, 5, 6, 7, 8, 3}) /* HelloAck packet */);
}

TEST(PacketNub, ProcessHandshakeFromHello) {
  TestTimer timer;
  TraceCout trace(&timer);
  ScopedRenderer scoped_renderer(&trace);
  ScopedSeverity scoped_severity{FLAGS_verbose ? Severity::DEBUG
                                               : Severity::INFO};

  StrictMock<MockPacketNub> nub(&timer, NodeId(2));

  EXPECT_CALL(nub, SendTo(123, PacketMatches(Slice::FromContainer(
                                   {3} /* HelloAck packet */))));
  nub.Process(timer.Now(), 123,
              Slice::WithInitializer(MockPacketNub::kHelloSize, [](uint8_t* p) {
                // Announce packet with the local node id
                memset(p, 0, MockPacketNub::kHelloSize);
                static const uint8_t prefix[] = {1, 2, 3, 4, 5, 6, 7, 8, 2,
                                                 1, 0, 0, 0, 0, 0, 0, 0};
                static_assert(sizeof(prefix) <= MockPacketNub::kHelloSize,
                              "PacketNub::kHelloSize too small");
                memcpy(p, prefix, sizeof(prefix));
              }));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&nub));

  std::shared_ptr<LinkPtr<>> link;
  EXPECT_CALL(nub, PublishMock(_)).WillOnce(SaveArg<0>(&link));
  EXPECT_CALL(nub, SendTo(123, PacketMatches(Slice::FromContainer(
                                   {0} /* Connected packet */))));
  nub.Process(
      timer.Now(), 123,
      Slice::FromContainer({1, 2, 3, 4, 5, 6, 7, 8, 0}) /* Connected packet */);
}

TEST(PacketNub, ProcessHandshakeFromAnnounceAndVerifyLink) {
  TestTimer timer;
  TraceCout trace(&timer);
  ScopedRenderer scoped_renderer(&trace);
  ScopedSeverity scoped_severity{FLAGS_verbose ? Severity::DEBUG
                                               : Severity::INFO};

  StrictMock<MockPacketNub> nub(&timer, NodeId(1));

  EXPECT_CALL(nub, SendTo(123, PaddedPacketMatches(Slice::FromContainer(
                                   {2, 1, 0, 0, 0, 0, 0, 0, 0}))));
  nub.Process(
      timer.Now(), 123,
      Slice::WithInitializer(MockPacketNub::kCallMeMaybeSize, [](uint8_t* p) {
        // Announce packet with the local node id
        memset(p, 0, MockPacketNub::kCallMeMaybeSize);
        static const uint8_t prefix[] = {1, 2, 3, 4, 5, 6, 7, 8, 1,
                                         2, 0, 0, 0, 0, 0, 0, 0};
        static_assert(sizeof(prefix) <= MockPacketNub::kCallMeMaybeSize,
                      "PacketNub::kHelloSize too small");
        memcpy(p, prefix, sizeof(prefix));
      }));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&nub));

  EXPECT_CALL(nub, SendTo(123, PacketMatches(Slice::FromContainer(
                                   {0}) /* Connected packet */)));
  std::shared_ptr<LinkPtr<>> link;
  EXPECT_CALL(nub, PublishMock(_)).WillOnce(SaveArg<0>(&link));
  nub.Process(
      timer.Now(), 123,
      Slice::FromContainer({1, 2, 3, 4, 5, 6, 7, 8, 3}) /* HelloAck packet */);
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&nub));

  EXPECT_EQ(NodeId(1), NodeId((*link)->GetLinkStatus().from));
  EXPECT_EQ(NodeId(2), NodeId((*link)->GetLinkStatus().to));
}

TEST(PacketNub, ProcessHandshakeFromHelloAndVerifyLink) {
  TestTimer timer;
  TraceCout trace(&timer);
  ScopedRenderer scoped_renderer(&trace);
  ScopedSeverity scoped_severity{FLAGS_verbose ? Severity::DEBUG
                                               : Severity::INFO};

  StrictMock<MockPacketNub> nub(&timer, NodeId(2));

  EXPECT_CALL(nub, SendTo(123, PacketMatches(Slice::FromContainer(
                                   {3} /* HelloAck packet */))));
  nub.Process(timer.Now(), 123,
              Slice::WithInitializer(MockPacketNub::kHelloSize, [](uint8_t* p) {
                // Announce packet with the local node id
                memset(p, 0, MockPacketNub::kHelloSize);
                static const uint8_t prefix[] = {1, 2, 3, 4, 5, 6, 7, 8, 2,
                                                 1, 0, 0, 0, 0, 0, 0, 0};
                static_assert(sizeof(prefix) <= MockPacketNub::kHelloSize,
                              "PacketNub::kHelloSize too small");
                memcpy(p, prefix, sizeof(prefix));
              }));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&nub));

  std::shared_ptr<LinkPtr<>> link;
  EXPECT_CALL(nub, PublishMock(_)).WillOnce(SaveArg<0>(&link));
  EXPECT_CALL(nub, SendTo(123, PacketMatches(Slice::FromContainer(
                                   {0}) /* Connected packet */)));
  nub.Process(
      timer.Now(), 123,
      Slice::FromContainer({1, 2, 3, 4, 5, 6, 7, 8, 0}) /* Connected packet */);
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&nub));

  EXPECT_EQ(NodeId(2), NodeId((*link)->GetLinkStatus().from));
  EXPECT_EQ(NodeId(1), NodeId((*link)->GetLinkStatus().to));
}

}  // namespace packet_nub_test
}  // namespace overnet

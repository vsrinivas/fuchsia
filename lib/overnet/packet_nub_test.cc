// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "packet_nub.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "test_timer.h"

using testing::_;
using testing::InvokeWithoutArgs;
using testing::Mock;
using testing::SaveArg;
using testing::StrictMock;

namespace overnet {
namespace packet_nub_test {

using FakeAddress = uint32_t;

class MockPacketNub : public PacketNub<FakeAddress, 1024> {
 public:
  MockPacketNub(Timer* timer, NodeId node)
      : PacketNub<FakeAddress, 1024>(timer, node), router_(timer, node, true) {}

  MOCK_METHOD2(SendTo, void(FakeAddress, Slice));
  MOCK_METHOD1(PublishMock, void(std::shared_ptr<overnet::Link>));

  void Publish(std::unique_ptr<overnet::Link> link) override final {
    PublishMock(std::shared_ptr<overnet::Link>(link.release()));
  }

  Router* GetRouter() override final { return &router_; }

 private:
  Router router_;
};

TEST(PacketNub, NoOp) {
  TestTimer timer;
  StrictMock<MockPacketNub> nub(&timer, NodeId(1));
}

TEST(PacketNub, InitiateBiggerNodeId) {
  TestTimer timer;
  StrictMock<MockPacketNub> nub(&timer, NodeId(1));

  const auto kHello =
      Slice::WithInitializer(MockPacketNub::kHelloSize, [](uint8_t* p) {
        // Hello packet with the local node id
        memset(p, 0, MockPacketNub::kHelloSize);
        static const uint8_t prefix[] = {2, 1, 0, 0, 0, 0, 0, 0, 0};
        static_assert(sizeof(prefix) <= MockPacketNub::kHelloSize,
                      "PacketNub::kHelloSize too small");
        memcpy(p, prefix, sizeof(prefix));
      });
  EXPECT_CALL(nub, SendTo(123, kHello));
  nub.Initiate(123, NodeId(2));

  int remaining = 4;
  EXPECT_CALL(nub, SendTo(123, kHello))
      .Times(remaining)
      .WillRepeatedly(InvokeWithoutArgs([&remaining]() { remaining--; }));
  for (int i = 0; i < 100 && remaining > 0; i++) {
    EXPECT_TRUE(timer.StepUntilNextEvent());
  }
}

TEST(PacketNub, InitiateSmallerNodeId) {
  TestTimer timer;
  StrictMock<MockPacketNub> nub(&timer, NodeId(2));

  const auto kAnnounce =
      Slice::WithInitializer(MockPacketNub::kCallMeMaybeSize, [](uint8_t* p) {
        // Announce packet with the local node id
        memset(p, 0, MockPacketNub::kCallMeMaybeSize);
        static const uint8_t prefix[] = {1, 2, 0, 0, 0, 0, 0, 0, 0};
        static_assert(sizeof(prefix) <= MockPacketNub::kCallMeMaybeSize,
                      "PacketNub::kHelloSize too small");
        memcpy(p, prefix, sizeof(prefix));
      });
  EXPECT_CALL(nub, SendTo(123, kAnnounce));
  nub.Initiate(123, NodeId(1));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&nub));

  int remaining = 4;
  EXPECT_CALL(nub, SendTo(123, kAnnounce))
      .Times(remaining)
      .WillRepeatedly(InvokeWithoutArgs([&remaining]() { remaining--; }));
  for (int i = 0; i < 100 && remaining > 0; i++) {
    EXPECT_TRUE(timer.StepUntilNextEvent());
  }
}

TEST(PacketNub, ProcessHandshakeFromAnnounce) {
  TestTimer timer;
  StrictMock<MockPacketNub> nub(&timer, NodeId(1));

  EXPECT_CALL(
      nub,
      SendTo(123,
             Slice::WithInitializer(MockPacketNub::kHelloSize, [](uint8_t* p) {
               // Hello packet with the local node id
               memset(p, 0, MockPacketNub::kHelloSize);
               static const uint8_t prefix[] = {2, 1, 0, 0, 0, 0, 0, 0, 0};
               static_assert(sizeof(prefix) <= MockPacketNub::kHelloSize,
                             "PacketNub::kHelloSize too small");
               memcpy(p, prefix, sizeof(prefix));
             })));
  nub.Process(
      timer.Now(), 123,
      Slice::WithInitializer(MockPacketNub::kCallMeMaybeSize, [](uint8_t* p) {
        // Announce packet with the local node id
        memset(p, 0, MockPacketNub::kCallMeMaybeSize);
        static const uint8_t prefix[] = {1, 2, 0, 0, 0, 0, 0, 0, 0};
        static_assert(sizeof(prefix) <= MockPacketNub::kCallMeMaybeSize,
                      "PacketNub::kHelloSize too small");
        memcpy(p, prefix, sizeof(prefix));
      }));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&nub));

  EXPECT_CALL(nub,
              SendTo(123, Slice::FromContainer({0}) /* Connected packet */));
  EXPECT_CALL(nub, PublishMock(_));
  nub.Process(timer.Now(), 123,
              Slice::FromContainer({3}) /* HelloAck packet */);
}

TEST(PacketNub, ProcessHandshakeFromHello) {
  TestTimer timer;
  StrictMock<MockPacketNub> nub(&timer, NodeId(2));

  EXPECT_CALL(nub,
              SendTo(123, Slice::FromContainer({3} /* HelloAck packet */)));
  nub.Process(timer.Now(), 123,
              Slice::WithInitializer(MockPacketNub::kHelloSize, [](uint8_t* p) {
                // Announce packet with the local node id
                memset(p, 0, MockPacketNub::kHelloSize);
                static const uint8_t prefix[] = {2, 1, 0, 0, 0, 0, 0, 0, 0};
                static_assert(sizeof(prefix) <= MockPacketNub::kHelloSize,
                              "PacketNub::kHelloSize too small");
                memcpy(p, prefix, sizeof(prefix));
              }));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&nub));

  EXPECT_CALL(nub, PublishMock(_));
  nub.Process(timer.Now(), 123,
              Slice::FromContainer({0}) /* Connected packet */);
}

TEST(PacketNub, ProcessHandshakeFromAnnounceAndVerifyLink) {
  TestTimer timer;
  StrictMock<MockPacketNub> nub(&timer, NodeId(1));

  EXPECT_CALL(
      nub,
      SendTo(123,
             Slice::WithInitializer(MockPacketNub::kHelloSize, [](uint8_t* p) {
               // Hello packet with the local node id
               memset(p, 0, MockPacketNub::kHelloSize);
               static const uint8_t prefix[] = {2, 1, 0, 0, 0, 0, 0, 0, 0};
               static_assert(sizeof(prefix) <= MockPacketNub::kHelloSize,
                             "PacketNub::kHelloSize too small");
               memcpy(p, prefix, sizeof(prefix));
             })));
  nub.Process(
      timer.Now(), 123,
      Slice::WithInitializer(MockPacketNub::kCallMeMaybeSize, [](uint8_t* p) {
        // Announce packet with the local node id
        memset(p, 0, MockPacketNub::kCallMeMaybeSize);
        static const uint8_t prefix[] = {1, 2, 0, 0, 0, 0, 0, 0, 0};
        static_assert(sizeof(prefix) <= MockPacketNub::kCallMeMaybeSize,
                      "PacketNub::kHelloSize too small");
        memcpy(p, prefix, sizeof(prefix));
      }));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&nub));

  EXPECT_CALL(nub,
              SendTo(123, Slice::FromContainer({0}) /* Connected packet */));
  std::shared_ptr<Link> link;
  EXPECT_CALL(nub, PublishMock(_)).WillOnce(SaveArg<0>(&link));
  nub.Process(timer.Now(), 123,
              Slice::FromContainer({3}) /* HelloAck packet */);
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&nub));

  EXPECT_EQ(NodeId(1), link->GetLinkMetrics().from());
  EXPECT_EQ(NodeId(2), link->GetLinkMetrics().to());
}

TEST(PacketNub, ProcessHandshakeFromHelloAndVerifyLink) {
  TestTimer timer;
  StrictMock<MockPacketNub> nub(&timer, NodeId(2));

  EXPECT_CALL(nub,
              SendTo(123, Slice::FromContainer({3} /* HelloAck packet */)));
  nub.Process(timer.Now(), 123,
              Slice::WithInitializer(MockPacketNub::kHelloSize, [](uint8_t* p) {
                // Announce packet with the local node id
                memset(p, 0, MockPacketNub::kHelloSize);
                static const uint8_t prefix[] = {2, 1, 0, 0, 0, 0, 0, 0, 0};
                static_assert(sizeof(prefix) <= MockPacketNub::kHelloSize,
                              "PacketNub::kHelloSize too small");
                memcpy(p, prefix, sizeof(prefix));
              }));
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&nub));

  std::shared_ptr<Link> link;
  EXPECT_CALL(nub, PublishMock(_)).WillOnce(SaveArg<0>(&link));
  EXPECT_CALL(nub,
              SendTo(123, Slice::FromContainer({0}) /* Connected packet */));
  nub.Process(timer.Now(), 123,
              Slice::FromContainer({0}) /* Connected packet */);
  EXPECT_TRUE(Mock::VerifyAndClearExpectations(&nub));

  EXPECT_EQ(NodeId(2), link->GetLinkMetrics().from());
  EXPECT_EQ(NodeId(1), link->GetLinkMetrics().to());
}

}  // namespace packet_nub_test
}  // namespace overnet

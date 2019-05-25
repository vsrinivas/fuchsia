// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/links/packet_stuffer.h"

#include <gtest/gtest.h>

#include "src/connectivity/overnet/lib/testing/test_timer.h"

namespace overnet {
namespace packet_stuffer_test {

constexpr TimeStamp kDummyTimestamp123 =
    TimeStamp::AfterEpoch(TimeDelta::FromMicroseconds(123));

Message DummyMessage() {
  return Message{std::move(RoutableMessage(NodeId(1)).AddDestination(
                     NodeId(2), StreamId(1), SeqNum(1, 1))),
                 ForwardingPayloadFactory(Slice::FromContainer({1, 2, 3})),
                 kDummyTimestamp123};
}

template <class T>
void IgnoreResult(T&& value) {}

TEST(PacketStuffer, NoOp) { PacketStuffer stuffer(NodeId(1), NodeId(2)); }

TEST(PacketStuffer, ForwardReturnValue) {
  PacketStuffer stuffer(NodeId(1), NodeId(2));
  EXPECT_TRUE(stuffer.Forward(DummyMessage()));
  EXPECT_FALSE(stuffer.Forward(DummyMessage()));
}

TEST(PacketStuffer, CanDropMessages) {
  PacketStuffer stuffer(NodeId(1), NodeId(2));
  EXPECT_FALSE(stuffer.HasPendingMessages());
  EXPECT_TRUE(stuffer.Forward(DummyMessage()));
  EXPECT_TRUE(stuffer.HasPendingMessages());
  stuffer.DropPendingMessages();
  EXPECT_FALSE(stuffer.HasPendingMessages());
}

struct PacketVerificationArgs {
  std::vector<Slice> messages;
  size_t max_serialize_length;
  Slice expected_bytes;
};

struct PacketStufferSerialization
    : public ::testing::TestWithParam<PacketVerificationArgs> {};

TEST_P(PacketStufferSerialization, Write) {
  PacketStuffer stuffer(NodeId(1), NodeId(2));

  int i = 0;
  for (auto msg : GetParam().messages) {
    IgnoreResult(stuffer.Forward(Message{
        std::move(RoutableMessage(NodeId(1)).AddDestination(
            NodeId(3), StreamId(1), SeqNum(i++, GetParam().messages.size()))),
        ForwardingPayloadFactory(msg), kDummyTimestamp123}));
  }

  EXPECT_EQ(stuffer.BuildPacket(
                LazySliceArgs{Border::None(), GetParam().max_serialize_length}),
            GetParam().expected_bytes);
}

TEST_P(PacketStufferSerialization, Read) {
  TestTimer timer;
  Router router(&timer, NodeId(2), false);
  std::vector<Slice> got_messages;

  class MockLink final : public Link {
   public:
    MockLink(std::vector<Slice>* got_messages) : got_messages_(got_messages) {}

    void Close(Callback<void> quiesced) override {}
    fuchsia::overnet::protocol::LinkStatus GetLinkStatus() override {
      return fuchsia::overnet::protocol::LinkStatus{NodeId(2).as_fidl(),
                                                    NodeId(3).as_fidl(), 1, 1};
    }
    const LinkStats* GetStats() const override { return nullptr; }

    void Forward(Message message) override {
      EXPECT_EQ(message.header.src(), NodeId(1));
      EXPECT_EQ(message.header.destinations().size(), size_t(1));
      EXPECT_EQ(message.header.destinations()[0].dst(), NodeId(3));
      got_messages_->emplace_back(message.make_payload(
          LazySliceArgs{Border::None(), std::numeric_limits<size_t>::max()}));
    }

   private:
    std::vector<Slice>* const got_messages_;
  };
  router.RegisterLink(MakeLink<MockLink>(&got_messages));

  while (!router.HasRouteTo(NodeId(3))) {
    timer.StepUntilNextEvent();
  }

  auto status = PacketStuffer(NodeId(1), NodeId(2))
                    .ParseAndForwardTo(kDummyTimestamp123,
                                       GetParam().expected_bytes, &router);

  EXPECT_TRUE(status.is_ok()) << status;
  EXPECT_EQ(got_messages, GetParam().messages);
}

INSTANTIATE_TEST_SUITE_P(PacketStufferSerializationSuite,
                         PacketStufferSerialization,
                         ::testing::Values(PacketVerificationArgs{
                             {Slice::FromContainer({1, 2, 3})},
                             256,
                             Slice::FromContainer({
                                 0x16, 0x10, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
                                 0x00, 0x00, 0x01, 0x00, 0x01, 0x02, 0x03,
                             })}));

}  // namespace packet_stuffer_test
}  // namespace overnet

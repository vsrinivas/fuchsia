// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/lib/protocol/routable_message.h"

#include "gtest/gtest.h"

namespace overnet {
namespace routing_header_test {

typedef std::vector<uint8_t> BinVec;

template <class T>
BinVec Encode(const T& x) {
  auto sz = x.wire_length();
  BinVec out;
  out.resize(sz);
  x.Write(out.data());
  return out;
}

template <class T>
BinVec EncodeVarint(const T& x) {
  auto sz = x.wire_length();
  BinVec out;
  out.resize(sz);
  x.Write(sz, out.data());
  return out;
}

TEST(BaseTypes, NodeId) {
  NodeId one(1);
  NodeId two(2);
  NodeId also_one(1);

  EXPECT_EQ(one, also_one);
  EXPECT_NE(one, two);
  EXPECT_EQ((BinVec{1, 0, 0, 0, 0, 0, 0, 0}), Encode(one));
  EXPECT_EQ((BinVec{2, 0, 0, 0, 0, 0, 0, 0}), Encode(two));
}

TEST(BaseTypes, StreamId) {
  StreamId one(1);
  StreamId two(2);
  StreamId also_one(1);

  EXPECT_EQ(one, also_one);
  EXPECT_NE(one, two);
  EXPECT_EQ((BinVec{1}), EncodeVarint(one));
  EXPECT_EQ((BinVec{2}), EncodeVarint(two));
}

static void RoundTrip(const RoutableMessage& rh, NodeId sender, NodeId receiver,
                      Slice payload) {
  auto enc = rh.Write(sender, receiver, payload);
  auto prs_status = RoutableMessage::Parse(enc, receiver, sender);
  EXPECT_TRUE(prs_status.is_ok()) << prs_status.AsStatus();
  EXPECT_EQ(rh, prs_status->message);
  EXPECT_EQ(payload, prs_status->payload);
}

TEST(RoutableMessage, PayloadWrite1) {
  auto rh = std::move(RoutableMessage(NodeId(1)).AddDestination(
      NodeId(2), StreamId(1), SeqNum(1, 10)));
  auto body = Slice::FromContainer({1, 2, 3});
  // encode locally
  EXPECT_EQ(Slice::FromContainer({// flags
                                  0,
                                  // dests {
                                  // [0] =
                                  //   stream_id
                                  1,
                                  //   seqnum
                                  1,
                                  // }
                                  // payload
                                  1, 2, 3}),
            rh.Write(NodeId(1), NodeId(2), body));
  RoundTrip(rh, NodeId(1), NodeId(2), body);
}

TEST(RoutableMessage, PayloadWrite2) {
  auto rh = std::move(RoutableMessage(NodeId(1)).AddDestination(
      NodeId(2), StreamId(1), SeqNum(1, 10)));
  auto body = Slice::FromContainer({1, 2, 3});
  // encode along a different route
  EXPECT_EQ(Slice::FromContainer({// flags
                                  (1 << 4),
                                  // src
                                  1, 0, 0, 0, 0, 0, 0, 0,
                                  // dests {
                                  // [0] =
                                  //   node
                                  2, 0, 0, 0, 0, 0, 0, 0,
                                  //   stream_id
                                  1,
                                  //   seqnum
                                  1,
                                  // }
                                  // payload
                                  1, 2, 3}),
            rh.Write(NodeId(4), NodeId(2), body));
  RoundTrip(rh, NodeId(4), NodeId(2), body);
}

TEST(RoutableMessage, PayloadWrite3) {
  auto rh =
      std::move(RoutableMessage(NodeId(1))
                    .AddDestination(NodeId(2), StreamId(1), SeqNum(1, 10))
                    .AddDestination(NodeId(3), StreamId(42), SeqNum(7, 10)));
  auto body = Slice::FromContainer({7, 8, 9});
  EXPECT_EQ(Slice::FromContainer({// flags
                                  (2 << 4),
                                  // src
                                  1, 0, 0, 0, 0, 0, 0, 0,
                                  // dests {
                                  // [0] =
                                  //   node
                                  2, 0, 0, 0, 0, 0, 0, 0,
                                  //   stream_id
                                  1,
                                  //   seqnum
                                  1,
                                  // [1] =
                                  //   node
                                  3, 0, 0, 0, 0, 0, 0, 0,
                                  //   stream_id
                                  42,
                                  //   seqnum
                                  7,
                                  // }
                                  // payload
                                  7, 8, 9}),
            rh.Write(NodeId(1), NodeId(2), body));
  RoundTrip(rh, NodeId(1), NodeId(2), body);
}

}  // namespace routing_header_test
}  // namespace overnet

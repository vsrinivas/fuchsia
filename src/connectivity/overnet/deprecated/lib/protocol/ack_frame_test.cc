// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/deprecated/lib/protocol/ack_frame.h"

#include "gtest/gtest.h"

namespace overnet {
namespace ack_frame_test {

std::vector<uint8_t> Encode(const AckFrame& h) {
  AckFrame::Writer w(&h);
  std::vector<uint8_t> v;
  v.resize(w.wire_length());
  uint8_t* end = w.Write(v.data());
  EXPECT_EQ(end, v.data() + v.size());
  return v;
}

void RoundTrip(const AckFrame& h, const std::vector<uint8_t>& expect) {
  auto v = Encode(h);
  EXPECT_EQ(expect, v);
  auto p = AckFrame::Parse(Slice::FromCopiedBuffer(v.data(), v.size()));
  EXPECT_TRUE(p.is_ok()) << p.AsStatus();
  EXPECT_EQ(h, *p.get());
}

TEST(AckFrame, NoNack) {
  AckFrame h(1, 0);
  RoundTrip(h, {1, 0});
}

TEST(AckFrame, OneNack) {
  AckFrame h(5, 10);
  h.AddNack(2);
  RoundTrip(h, {5, 20, 3, 1});
  EXPECT_EQ(h.nack_seqs().AsVector(), std::vector<uint64_t>({2}));
}

TEST(AckFrame, ThreeNacks) {
  AckFrame h(5, 42);
  h.AddNack(4);
  h.AddNack(3);
  h.AddNack(2);
  RoundTrip(h, {5, 84, 1, 3});
  EXPECT_EQ(h.nack_seqs().AsVector(), std::vector<uint64_t>({2, 3, 4}));
}

TEST(AckFrame, TwoBlocks) {
  AckFrame h(20, 42);
  h.AddNack(15);
  h.AddNack(14);
  h.AddNack(13);
  h.AddNack(5);
  h.AddNack(4);
  h.AddNack(3);
  h.AddNack(2);
  h.AddNack(1);
  RoundTrip(h, {20, 84, 5, 3, 7, 5});
  EXPECT_EQ(h.nack_seqs().AsVector(), std::vector<uint64_t>({1, 2, 3, 4, 5, 13, 14, 15}));
}

TEST(AckFrame, FuzzedExamples) {
  auto test = [](std::initializer_list<uint8_t> bytes) {
    auto input = Slice::FromContainer(bytes);
    std::cout << "Test: " << input << "\n";
    if (auto p = AckFrame::Parse(input); p.is_ok()) {
      {
        auto ns = p->nack_seqs();
        const auto begin = ns.begin();
        const auto end = ns.end();
        for (auto it = begin; it != end; ++it) {
          [](auto) {}(*it);
        }
      }
      std::cout << "Parsed: " << *p << "\n";
      auto written = Slice::FromWriters(AckFrame::Writer(p.get()));
      auto p2 = AckFrame::Parse(written);
      EXPECT_TRUE(p2.is_ok());
      EXPECT_EQ(*p, *p2);
    } else {
      std::cout << "Parse error: " << p.AsStatus() << "\n";
    }
  };
  test({0x0a, 0x0a, 0x00, 0x00});
  test({0xc1, 0xe0, 0x00, 0x2d});
  test({0x80, 0xcd, 0xcd, 0xcd, 0xcd, 0x2b, 0x00, 0x2f, 0xcd, 0xcd, 0xf9, 0xe4, 0x00, 0x51});
  test({0x65, 0x01, 0x01, 0x02});
}

}  // namespace ack_frame_test
}  // namespace overnet

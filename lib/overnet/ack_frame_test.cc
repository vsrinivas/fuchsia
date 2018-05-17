// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ack_frame.h"
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
  EXPECT_TRUE(p.is_ok());
  EXPECT_EQ(h, *p.get());
}

TEST(AckFrame, NoNack) {
  AckFrame h(1, 0);
  RoundTrip(h, {1, 0});
}

TEST(AckFrame, OneNack) {
  AckFrame h(5, 10);
  h.AddNack(2);
  RoundTrip(h, {5, 10, 3});
}

TEST(AckFrame, ThreeNacks) {
  AckFrame h(5, 42);
  h.AddNack(4);
  h.AddNack(3);
  h.AddNack(2);
  RoundTrip(h, {5, 42, 1, 1, 1});
}

}  // namespace ack_frame_test
}  // namespace overnet

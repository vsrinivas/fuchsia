// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/overnet/deprecated/lib/labels/seq_num.h"

#include "gtest/gtest.h"

namespace overnet {
namespace seq_num_test {

typedef std::vector<uint8_t> BinVec;

template <class T>
BinVec Encode(const T& x) {
  auto sz = x.wire_length();
  BinVec out;
  out.resize(sz);
  x.Write(out.data());
  return out;
}

TEST(SeqNum, Basics) {
  SeqNum seq_1_1(1, 1);
  EXPECT_EQ(1ull, seq_1_1.Reconstruct(1));
  EXPECT_EQ((BinVec{1}), Encode(seq_1_1));
}

TEST(SeqNum, Edges) { EXPECT_EQ(68u, SeqNum(68, 68 - 63).Reconstruct(59)); }

}  // namespace seq_num_test
}  // namespace overnet

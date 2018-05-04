// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "varint.h"
#include <string>
#include "gtest/gtest.h"

namespace overnet {
namespace varint {
namespace varint_test {

typedef std::vector<uint8_t> BinVec;

TEST(Varint, WireSizeFor) {
  EXPECT_EQ(1, WireSizeFor(0));
  EXPECT_EQ(1, WireSizeFor(127));
  EXPECT_EQ(2, WireSizeFor(128));
  EXPECT_EQ(3, WireSizeFor(1ull << 14));
  EXPECT_EQ(4, WireSizeFor(1ull << 21));
  EXPECT_EQ(5, WireSizeFor(1ull << 28));
  EXPECT_EQ(6, WireSizeFor(1ull << 35));
  EXPECT_EQ(7, WireSizeFor(1ull << 42));
  EXPECT_EQ(8, WireSizeFor(1ull << 49));
  EXPECT_EQ(9, WireSizeFor(1ull << 56));
  EXPECT_EQ(10, WireSizeFor(1ull << 63));
  EXPECT_EQ(10, WireSizeFor(0xffffffffffffffffull));
}

template <class T>
BinVec Encode(T x) {
  auto sz = WireSizeFor(x);
  BinVec out;
  out.resize(sz);
  Write(x, sz, out.data());
  return out;
}

TEST(Varint, SimpleWrite) {
  EXPECT_EQ(BinVec{127}, Encode(127));
  EXPECT_EQ((BinVec{0xac, 0x02}), Encode(300));
  EXPECT_EQ((BinVec{0xf8, 0xf3, 0xeb, 0xdf, 0xcf, 0xbf, 0xbf, 0xff, 0xff, 0x1}),
            Encode(0xfffefdfcfbfaf9f8ull));
}

}  // namespace varint_test
}  // namespace varint
}  // namespace overnet

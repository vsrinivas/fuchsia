// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "node_id.h"
#include <random>
#include "gtest/gtest.h"

namespace overnet {
namespace node_id_tests {

TEST(NodeId, ToFromString) {
  auto test = [](uint64_t value) {
    NodeId src(value);
    auto str = src.ToString();
    auto dst = NodeId::FromString(str);
    EXPECT_TRUE(dst.is_ok()) << dst;
    EXPECT_EQ(src, *dst);
  };
  test(0);
  test(0xffffffffffffffffull);
  std::mt19937_64 rng;
  std::uniform_int_distribution<uint64_t> rand;
  for (int i = 0; i < 1000; i++) {
    test(rand(rng));
  }
  EXPECT_FALSE(NodeId::FromString("abcdef").is_ok());
  EXPECT_FALSE(NodeId::FromString("[").is_ok());
  EXPECT_FALSE(NodeId::FromString("[ab").is_ok());
  EXPECT_FALSE(NodeId::FromString("[abcd").is_ok());
  EXPECT_FALSE(NodeId::FromString("[abcd_").is_ok());
  EXPECT_FALSE(NodeId::FromString("[abcd^").is_ok());
  EXPECT_FALSE(NodeId::FromString("[abcde").is_ok());
  EXPECT_FALSE(NodeId::FromString("[abcd_0123").is_ok());
  EXPECT_FALSE(NodeId::FromString("[abcd_0123_").is_ok());
  EXPECT_FALSE(NodeId::FromString("[abcd_0123_defg").is_ok());
  EXPECT_FALSE(NodeId::FromString("[abcd_0123_abcd_0123").is_ok());
  EXPECT_TRUE(NodeId::FromString("[abcd_0123_abcd_0123]").is_ok());
  EXPECT_FALSE(NodeId::FromString("[abcd_0123_abcd_0123]!").is_ok());
}

}  // namespace node_id_tests
}  // namespace overnet
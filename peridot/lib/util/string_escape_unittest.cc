// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/util/string_escape.h"

#include "gtest/gtest.h"

using fxl::StringView;

namespace modular {
namespace {

TEST(EscapeStringTest, EscapeUnescape) {
  StringView original = "ABCDEFGHIJKLMNOPQRST";
  std::string expected = "ABCD|EFGHI|JKLMNOPQRST";

  EXPECT_EQ(expected, StringEscape(original, "EJ", '|'));
  EXPECT_EQ(original, StringUnescape(expected, '|'));

  EXPECT_EQ("a", StringUnescape("|a", '|'));
}

TEST(EscapeStringTest, SplitSimple) {
  auto result = SplitEscapedString("a_b|_c_d", '_', '|');
  EXPECT_EQ(3u, result.size());
  EXPECT_EQ("a", result[0]);
  EXPECT_EQ("b|_c", result[1]);
  EXPECT_EQ("d", result[2]);
}

TEST(EscapeStringTest, SplitEdge) {
  auto result = SplitEscapedString("a_", '_', '|');
  EXPECT_EQ(1u, result.size());
  EXPECT_EQ("a", result[0]);
}

TEST(EscapeStringTest, SplitWithEmpties) {
  auto result = SplitEscapedString("a___b", '_', '|');
  EXPECT_EQ(4u, result.size());
  EXPECT_EQ("a", result[0]);
  EXPECT_EQ("", result[1]);
  EXPECT_EQ("", result[2]);
  EXPECT_EQ("b", result[3]);
}

}  // namespace
}  // namespace modular

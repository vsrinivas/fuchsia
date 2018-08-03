// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/expr_tokenizer.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(ExprTokenizer, Empty) {
  ExprTokenizer t((std::string()));

  EXPECT_TRUE(t.Tokenize());
  EXPECT_FALSE(t.err().has_error()) << t.err().msg();
  EXPECT_TRUE(t.tokens().empty());
}

TEST(ExprTokenizer, InvalidChar) {
  // Offsets:      012345
  ExprTokenizer t("1234 @ hello");

  EXPECT_FALSE(t.Tokenize());
  EXPECT_TRUE(t.err().has_error());
  EXPECT_EQ("Invalid character '@' in expression.", t.err().msg());
  EXPECT_EQ(5u, t.error_location());
}

TEST(ExprTokenizer, Punctuation) {
  // Char offsets: 0 1234567890123456
  // Token #'s:      0 1 2  3 45 67 8
  ExprTokenizer t("\n. * -> & () [] -");

  EXPECT_TRUE(t.Tokenize());
  EXPECT_FALSE(t.err().has_error()) << t.err().msg();
  const auto& tokens = t.tokens();
  ASSERT_EQ(9u, tokens.size());

  EXPECT_EQ(ExprToken::kDot, tokens[0].type());
  EXPECT_EQ(".", tokens[0].value());
  EXPECT_EQ(1u, tokens[0].byte_offset());

  EXPECT_EQ(ExprToken::kStar, tokens[1].type());
  EXPECT_EQ("*", tokens[1].value());
  EXPECT_EQ(3u, tokens[1].byte_offset());

  EXPECT_EQ(ExprToken::kArrow, tokens[2].type());
  EXPECT_EQ("->", tokens[2].value());
  EXPECT_EQ(5u, tokens[2].byte_offset());

  EXPECT_EQ(ExprToken::kAmpersand, tokens[3].type());
  EXPECT_EQ("&", tokens[3].value());
  EXPECT_EQ(8u, tokens[3].byte_offset());

  EXPECT_EQ(ExprToken::kLeftParen, tokens[4].type());
  EXPECT_EQ("(", tokens[4].value());
  EXPECT_EQ(10u, tokens[4].byte_offset());

  EXPECT_EQ(ExprToken::kRightParen, tokens[5].type());
  EXPECT_EQ(")", tokens[5].value());
  EXPECT_EQ(11u, tokens[5].byte_offset());

  EXPECT_EQ(ExprToken::kLeftSquare, tokens[6].type());
  EXPECT_EQ("[", tokens[6].value());
  EXPECT_EQ(13u, tokens[6].byte_offset());

  EXPECT_EQ(ExprToken::kRightSquare, tokens[7].type());
  EXPECT_EQ("]", tokens[7].value());
  EXPECT_EQ(14u, tokens[7].byte_offset());

  EXPECT_EQ(ExprToken::kMinus, tokens[8].type());
  EXPECT_EQ("-", tokens[8].value());
  EXPECT_EQ(16u, tokens[8].byte_offset());
}

TEST(ExprTokenizer, ValidIntegers) {
  // Char offsets: 012345678901
  // Token #'s:    0    12 34 5
  ExprTokenizer t("1234 -56-1 0x5a4bcdef");

  EXPECT_TRUE(t.Tokenize());
  EXPECT_FALSE(t.err().has_error()) << t.err().msg();
  const auto& tokens = t.tokens();
  ASSERT_EQ(6u, tokens.size());

  EXPECT_EQ(ExprToken::kInteger, tokens[0].type());
  EXPECT_EQ("1234", tokens[0].value());
  EXPECT_EQ(0u, tokens[0].byte_offset());

  EXPECT_EQ(ExprToken::kMinus, tokens[1].type());
  EXPECT_EQ("-", tokens[1].value());
  EXPECT_EQ(5u, tokens[1].byte_offset());

  EXPECT_EQ(ExprToken::kInteger, tokens[2].type());
  EXPECT_EQ("56", tokens[2].value());
  EXPECT_EQ(6u, tokens[2].byte_offset());

  EXPECT_EQ(ExprToken::kMinus, tokens[3].type());
  EXPECT_EQ("-", tokens[3].value());
  EXPECT_EQ(8u, tokens[3].byte_offset());

  EXPECT_EQ(ExprToken::kInteger, tokens[4].type());
  EXPECT_EQ("1", tokens[4].value());
  EXPECT_EQ(9u, tokens[4].byte_offset());

  EXPECT_EQ(ExprToken::kInteger, tokens[5].type());
  EXPECT_EQ("0x5a4bcdef", tokens[5].value());
  EXPECT_EQ(11u, tokens[5].byte_offset());
}

TEST(ExprTokenizer, Names) {
  // Char offsets: 0123456789012345678901
  // Token #'s:     0   12    3 4       5
  ExprTokenizer t(" name(hello] goodbye a");

  EXPECT_TRUE(t.Tokenize());
  EXPECT_FALSE(t.err().has_error()) << t.err().msg();
  const auto& tokens = t.tokens();
  ASSERT_EQ(6u, tokens.size());

  EXPECT_EQ(ExprToken::kName, tokens[0].type());
  EXPECT_EQ("name", tokens[0].value());
  EXPECT_EQ(1u, tokens[0].byte_offset());

  EXPECT_EQ(ExprToken::kLeftParen, tokens[1].type());
  EXPECT_EQ("(", tokens[1].value());
  EXPECT_EQ(5u, tokens[1].byte_offset());

  EXPECT_EQ(ExprToken::kName, tokens[2].type());
  EXPECT_EQ("hello", tokens[2].value());
  EXPECT_EQ(6u, tokens[2].byte_offset());

  EXPECT_EQ(ExprToken::kRightSquare, tokens[3].type());
  EXPECT_EQ("]", tokens[3].value());
  EXPECT_EQ(11u, tokens[3].byte_offset());

  EXPECT_EQ(ExprToken::kName, tokens[4].type());
  EXPECT_EQ("goodbye", tokens[4].value());
  EXPECT_EQ(13u, tokens[4].byte_offset());

  EXPECT_EQ(ExprToken::kName, tokens[5].type());
  EXPECT_EQ("a", tokens[5].value());
  EXPECT_EQ(21u, tokens[5].byte_offset());
}

}  // namespace zxdb

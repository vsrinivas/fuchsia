// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/expr_tokenizer.h"

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
  ExprTokenizer t("1234 ` hello");

  EXPECT_FALSE(t.Tokenize());
  EXPECT_TRUE(t.err().has_error());
  EXPECT_EQ(
      "Invalid character '`' in expression.\n"
      "  1234 ` hello\n"
      "       ^",
      t.err().msg());
  EXPECT_EQ(5u, t.error_location());
}

TEST(ExprTokenizer, Punctuation) {
  // Char offsets: 0 12345678901234567890123456789012
  // Token #'s:      0 1 2  3 45 67 8 9  0  1 2 3 4 5
  ExprTokenizer t("\n. * -> & () [] - :: == = % + ^ /");

  EXPECT_TRUE(t.Tokenize());
  EXPECT_FALSE(t.err().has_error()) << t.err().msg();
  const auto& tokens = t.tokens();
  ASSERT_EQ(16u, tokens.size());

  EXPECT_EQ(ExprTokenType::kDot, tokens[0].type());
  EXPECT_EQ(".", tokens[0].value());
  EXPECT_EQ(1u, tokens[0].byte_offset());

  EXPECT_EQ(ExprTokenType::kStar, tokens[1].type());
  EXPECT_EQ("*", tokens[1].value());
  EXPECT_EQ(3u, tokens[1].byte_offset());

  EXPECT_EQ(ExprTokenType::kArrow, tokens[2].type());
  EXPECT_EQ("->", tokens[2].value());
  EXPECT_EQ(5u, tokens[2].byte_offset());

  EXPECT_EQ(ExprTokenType::kAmpersand, tokens[3].type());
  EXPECT_EQ("&", tokens[3].value());
  EXPECT_EQ(8u, tokens[3].byte_offset());

  EXPECT_EQ(ExprTokenType::kLeftParen, tokens[4].type());
  EXPECT_EQ("(", tokens[4].value());
  EXPECT_EQ(10u, tokens[4].byte_offset());

  EXPECT_EQ(ExprTokenType::kRightParen, tokens[5].type());
  EXPECT_EQ(")", tokens[5].value());
  EXPECT_EQ(11u, tokens[5].byte_offset());

  EXPECT_EQ(ExprTokenType::kLeftSquare, tokens[6].type());
  EXPECT_EQ("[", tokens[6].value());
  EXPECT_EQ(13u, tokens[6].byte_offset());

  EXPECT_EQ(ExprTokenType::kRightSquare, tokens[7].type());
  EXPECT_EQ("]", tokens[7].value());
  EXPECT_EQ(14u, tokens[7].byte_offset());

  EXPECT_EQ(ExprTokenType::kMinus, tokens[8].type());
  EXPECT_EQ("-", tokens[8].value());
  EXPECT_EQ(16u, tokens[8].byte_offset());

  EXPECT_EQ(ExprTokenType::kColonColon, tokens[9].type());
  EXPECT_EQ("::", tokens[9].value());
  EXPECT_EQ(18u, tokens[9].byte_offset());

  EXPECT_EQ(ExprTokenType::kEquality, tokens[10].type());
  EXPECT_EQ("==", tokens[10].value());
  EXPECT_EQ(21u, tokens[10].byte_offset());

  EXPECT_EQ(ExprTokenType::kEquals, tokens[11].type());
  EXPECT_EQ("=", tokens[11].value());
  EXPECT_EQ(24u, tokens[11].byte_offset());

  EXPECT_EQ(ExprTokenType::kPercent, tokens[12].type());
  EXPECT_EQ("%", tokens[12].value());
  EXPECT_EQ(26u, tokens[12].byte_offset());

  EXPECT_EQ(ExprTokenType::kPlus, tokens[13].type());
  EXPECT_EQ("+", tokens[13].value());
  EXPECT_EQ(28u, tokens[13].byte_offset());

  EXPECT_EQ(ExprTokenType::kCaret, tokens[14].type());
  EXPECT_EQ("^", tokens[14].value());
  EXPECT_EQ(30u, tokens[14].byte_offset());

  EXPECT_EQ(ExprTokenType::kSlash, tokens[15].type());
  EXPECT_EQ("/", tokens[15].value());
  EXPECT_EQ(32u, tokens[15].byte_offset());
}

TEST(ExprTokenizer, LessGreater) {
  // "<<" is identified normally, but ">>" is always tokenized as two separate '>' tokens. The
  // parser will disambiguate in the next phase which we don't see here.

  // Char offsets: 0123456789012345678901234567890123456
  // Token #'s:    0 1 2  34 5 6789
  ExprTokenizer t("< > << >> <<<>>>");

  EXPECT_TRUE(t.Tokenize());
  EXPECT_FALSE(t.err().has_error()) << t.err().msg();
  const auto& tokens = t.tokens();
  ASSERT_EQ(10u, tokens.size());

  EXPECT_EQ(ExprTokenType::kLess, tokens[0].type());
  EXPECT_EQ("<", tokens[0].value());
  EXPECT_EQ(0u, tokens[0].byte_offset());

  EXPECT_EQ(ExprTokenType::kGreater, tokens[1].type());
  EXPECT_EQ(">", tokens[1].value());
  EXPECT_EQ(2u, tokens[1].byte_offset());

  EXPECT_EQ(ExprTokenType::kShiftLeft, tokens[2].type());
  EXPECT_EQ("<<", tokens[2].value());
  EXPECT_EQ(4u, tokens[2].byte_offset());

  EXPECT_EQ(ExprTokenType::kGreater, tokens[3].type());
  EXPECT_EQ(">", tokens[3].value());
  EXPECT_EQ(7u, tokens[3].byte_offset());

  EXPECT_EQ(ExprTokenType::kGreater, tokens[4].type());
  EXPECT_EQ(">", tokens[4].value());
  EXPECT_EQ(8u, tokens[4].byte_offset());

  EXPECT_EQ(ExprTokenType::kShiftLeft, tokens[5].type());
  EXPECT_EQ("<<", tokens[5].value());
  EXPECT_EQ(10u, tokens[5].byte_offset());

  EXPECT_EQ(ExprTokenType::kLess, tokens[6].type());
  EXPECT_EQ("<", tokens[6].value());
  EXPECT_EQ(12u, tokens[6].byte_offset());

  EXPECT_EQ(ExprTokenType::kGreater, tokens[7].type());
  EXPECT_EQ(">", tokens[7].value());
  EXPECT_EQ(13u, tokens[7].byte_offset());

  EXPECT_EQ(ExprTokenType::kGreater, tokens[8].type());
  EXPECT_EQ(">", tokens[8].value());
  EXPECT_EQ(14u, tokens[8].byte_offset());

  EXPECT_EQ(ExprTokenType::kGreater, tokens[9].type());
  EXPECT_EQ(">", tokens[9].value());
  EXPECT_EQ(15u, tokens[9].byte_offset());
}

TEST(ExprTokenizer, Integers) {
  // Note that the tokenizer doesn't validate numbers, so "7hello" is extracted as a number token.
  // The complex rules for number validation and conversion will be done at a higher layer.

  // Char offsets: 01234567890123456789012345678901
  // Token #'s:    0    12 34 5          6        7
  ExprTokenizer t("1234 -56-1 0x5a4bcdef 0o123llu 7hello ");

  EXPECT_TRUE(t.Tokenize());
  EXPECT_FALSE(t.err().has_error()) << t.err().msg();
  const auto& tokens = t.tokens();
  ASSERT_EQ(8u, tokens.size());

  EXPECT_EQ(ExprTokenType::kInteger, tokens[0].type());
  EXPECT_EQ("1234", tokens[0].value());
  EXPECT_EQ(0u, tokens[0].byte_offset());

  EXPECT_EQ(ExprTokenType::kMinus, tokens[1].type());
  EXPECT_EQ("-", tokens[1].value());
  EXPECT_EQ(5u, tokens[1].byte_offset());

  EXPECT_EQ(ExprTokenType::kInteger, tokens[2].type());
  EXPECT_EQ("56", tokens[2].value());
  EXPECT_EQ(6u, tokens[2].byte_offset());

  EXPECT_EQ(ExprTokenType::kMinus, tokens[3].type());
  EXPECT_EQ("-", tokens[3].value());
  EXPECT_EQ(8u, tokens[3].byte_offset());

  EXPECT_EQ(ExprTokenType::kInteger, tokens[4].type());
  EXPECT_EQ("1", tokens[4].value());
  EXPECT_EQ(9u, tokens[4].byte_offset());

  EXPECT_EQ(ExprTokenType::kInteger, tokens[5].type());
  EXPECT_EQ("0x5a4bcdef", tokens[5].value());
  EXPECT_EQ(11u, tokens[5].byte_offset());

  EXPECT_EQ(ExprTokenType::kInteger, tokens[6].type());
  EXPECT_EQ("0o123llu", tokens[6].value());
  EXPECT_EQ(22u, tokens[6].byte_offset());

  EXPECT_EQ(ExprTokenType::kInteger, tokens[7].type());
  EXPECT_EQ("7hello", tokens[7].value());
  EXPECT_EQ(31u, tokens[7].byte_offset());
}

TEST(ExprTokenizer, OtherLiterals) {
  // Char offsets: 01234567890123456789012345678901234567890123
  // Token #'s:    0    1    2   34     5      6     7        8
  ExprTokenizer t("true True true)false falsey const volatile restrict");

  EXPECT_TRUE(t.Tokenize());
  EXPECT_FALSE(t.err().has_error()) << t.err().msg();
  const auto& tokens = t.tokens();
  ASSERT_EQ(9u, tokens.size());

  EXPECT_EQ(ExprTokenType::kTrue, tokens[0].type());
  EXPECT_EQ("true", tokens[0].value());
  EXPECT_EQ(0u, tokens[0].byte_offset());

  EXPECT_EQ(ExprTokenType::kName, tokens[1].type());
  EXPECT_EQ("True", tokens[1].value());
  EXPECT_EQ(5u, tokens[1].byte_offset());

  EXPECT_EQ(ExprTokenType::kTrue, tokens[2].type());
  EXPECT_EQ("true", tokens[2].value());
  EXPECT_EQ(10u, tokens[2].byte_offset());

  EXPECT_EQ(ExprTokenType::kRightParen, tokens[3].type());
  EXPECT_EQ(")", tokens[3].value());
  EXPECT_EQ(14u, tokens[3].byte_offset());

  EXPECT_EQ(ExprTokenType::kFalse, tokens[4].type());
  EXPECT_EQ("false", tokens[4].value());
  EXPECT_EQ(15u, tokens[4].byte_offset());

  EXPECT_EQ(ExprTokenType::kName, tokens[5].type());
  EXPECT_EQ("falsey", tokens[5].value());
  EXPECT_EQ(21u, tokens[5].byte_offset());

  EXPECT_EQ(ExprTokenType::kConst, tokens[6].type());
  EXPECT_EQ("const", tokens[6].value());
  EXPECT_EQ(28u, tokens[6].byte_offset());

  EXPECT_EQ(ExprTokenType::kVolatile, tokens[7].type());
  EXPECT_EQ("volatile", tokens[7].value());
  EXPECT_EQ(34u, tokens[7].byte_offset());

  EXPECT_EQ(ExprTokenType::kRestrict, tokens[8].type());
  EXPECT_EQ("restrict", tokens[8].value());
  EXPECT_EQ(43u, tokens[8].byte_offset());
}

TEST(ExprTokenizer, Names) {
  // Char offsets: 0123456789012345678901
  // Token #'s:     0   12    3 4       5
  ExprTokenizer t(" name(hello] goodbye a");

  EXPECT_TRUE(t.Tokenize());
  EXPECT_FALSE(t.err().has_error()) << t.err().msg();
  const auto& tokens = t.tokens();
  ASSERT_EQ(6u, tokens.size());

  EXPECT_EQ(ExprTokenType::kName, tokens[0].type());
  EXPECT_EQ("name", tokens[0].value());
  EXPECT_EQ(1u, tokens[0].byte_offset());

  EXPECT_EQ(ExprTokenType::kLeftParen, tokens[1].type());
  EXPECT_EQ("(", tokens[1].value());
  EXPECT_EQ(5u, tokens[1].byte_offset());

  EXPECT_EQ(ExprTokenType::kName, tokens[2].type());
  EXPECT_EQ("hello", tokens[2].value());
  EXPECT_EQ(6u, tokens[2].byte_offset());

  EXPECT_EQ(ExprTokenType::kRightSquare, tokens[3].type());
  EXPECT_EQ("]", tokens[3].value());
  EXPECT_EQ(11u, tokens[3].byte_offset());

  EXPECT_EQ(ExprTokenType::kName, tokens[4].type());
  EXPECT_EQ("goodbye", tokens[4].value());
  EXPECT_EQ(13u, tokens[4].byte_offset());

  EXPECT_EQ(ExprTokenType::kName, tokens[5].type());
  EXPECT_EQ("a", tokens[5].value());
  EXPECT_EQ(21u, tokens[5].byte_offset());
}

TEST(ExprTokenizer, GetErrorContext) {
  EXPECT_EQ(
      "  foo\n"
      "  ^",
      ExprTokenizer::GetErrorContext("foo", 0));
  EXPECT_EQ(
      "  foo\n"
      "    ^",
      ExprTokenizer::GetErrorContext("foo", 2));

  // One-past-the end is allowed.
  EXPECT_EQ(
      "  foo\n"
      "     ^",
      ExprTokenizer::GetErrorContext("foo", 3));
}

// Tests that C and Rust tokens are separated.
TEST(ExprTokenizer, Language) {
  // Test that "reinterpret_cast is valid in C but not in Rust.
  ExprTokenizer c("reinterpret_cast", ExprLanguage::kC);
  EXPECT_TRUE(c.Tokenize());
  EXPECT_FALSE(c.err().has_error()) << c.err().msg();
  auto tokens = c.tokens();
  ASSERT_EQ(1u, tokens.size());
  EXPECT_EQ(ExprTokenType::kReinterpretCast, tokens[0].type());

  // In Rust it's interpreted as a regular name.
  ExprTokenizer r("reinterpret_cast", ExprLanguage::kRust);
  EXPECT_TRUE(r.Tokenize());
  EXPECT_FALSE(r.err().has_error()) << r.err().msg();
  tokens = r.tokens();
  ASSERT_EQ(1u, tokens.size());
  EXPECT_EQ(ExprTokenType::kName, tokens[0].type());

  // Currently we don't have any Rust-only tokens. When we add one we should test that it works only
  // in Rust mode.
}

}  // namespace zxdb

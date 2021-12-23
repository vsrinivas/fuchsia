// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/parse_string.h"

#include <gtest/gtest.h>

namespace zxdb {

namespace {

ErrOr<std::string> Parse(ExprLanguage lang, std::string_view input, size_t* in_out_cur,
                         size_t* error_location) {
  std::optional<StringOrCharLiteralBegin> info =
      DoesBeginStringOrCharLiteral(lang, input, *in_out_cur);
  if (!info)
    return Err("Test harness says this does not begin a string.");

  return ParseStringOrCharLiteral(input, *info, in_out_cur, error_location);
}

}  // namespace

TEST(ParseString, DoesBeginStringLiteral_C) {
  std::optional<StringOrCharLiteralBegin> info =
      DoesBeginStringOrCharLiteral(ExprLanguage::kC, "", 0);
  EXPECT_FALSE(info);

  info = DoesBeginStringOrCharLiteral(ExprLanguage::kC, "hello", 0);
  EXPECT_FALSE(info);

  info = DoesBeginStringOrCharLiteral(ExprLanguage::kC, "\"", 0);
  ASSERT_TRUE(info);
  EXPECT_EQ(ExprTokenType::kStringLiteral, info->token_type);
  EXPECT_FALSE(info->is_raw);
  EXPECT_EQ(0u, info->string_begin);
  EXPECT_EQ(1u, info->contents_begin);

  info = DoesBeginStringOrCharLiteral(ExprLanguage::kC, "  \"string", 2);
  ASSERT_TRUE(info);
  EXPECT_FALSE(info->is_raw);
  EXPECT_EQ(3u, info->contents_begin);

  // Incomplete raw prefix.
  info = DoesBeginStringOrCharLiteral(ExprLanguage::kC, "R\"", 0);
  EXPECT_FALSE(info);
  info = DoesBeginStringOrCharLiteral(ExprLanguage::kC, "R\"foo \"", 0);
  EXPECT_FALSE(info);

  // Delimiters can not include some characters.
  info = DoesBeginStringOrCharLiteral(ExprLanguage::kC, "R\" () \"", 0);
  EXPECT_FALSE(info);
  info = DoesBeginStringOrCharLiteral(ExprLanguage::kC, "R\"\\a()\\a\"", 0);
  EXPECT_FALSE(info);

  // Valid raw prefix.
  info = DoesBeginStringOrCharLiteral(ExprLanguage::kC, "R\"(", 0);
  ASSERT_TRUE(info);
  EXPECT_TRUE(info->is_raw);
  EXPECT_EQ("", info->raw_marker);
  EXPECT_EQ(0u, info->string_begin);
  EXPECT_EQ(3u, info->contents_begin);

  info = DoesBeginStringOrCharLiteral(ExprLanguage::kC, "  R\"delimiter( ", 2);
  ASSERT_TRUE(info);
  EXPECT_TRUE(info->is_raw);
  EXPECT_EQ("delimiter", info->raw_marker);
  EXPECT_EQ(2u, info->string_begin);
  EXPECT_EQ(14u, info->contents_begin);
}

TEST(ParseString, DoesBeginStringLiteral_Rust) {
  std::optional<StringOrCharLiteralBegin> info =
      DoesBeginStringOrCharLiteral(ExprLanguage::kRust, "", 0);
  EXPECT_FALSE(info);

  info = DoesBeginStringOrCharLiteral(ExprLanguage::kRust, "hello", 0);
  EXPECT_FALSE(info);

  info = DoesBeginStringOrCharLiteral(ExprLanguage::kRust, "\"", 0);
  ASSERT_TRUE(info);
  EXPECT_EQ(ExprTokenType::kStringLiteral, info->token_type);
  EXPECT_FALSE(info->is_raw);
  EXPECT_EQ(1u, info->contents_begin);

  info = DoesBeginStringOrCharLiteral(ExprLanguage::kRust, "  \"string", 2);
  ASSERT_TRUE(info);
  EXPECT_FALSE(info->is_raw);
  EXPECT_EQ(3u, info->contents_begin);

  // Incomplete raw prefix.
  info = DoesBeginStringOrCharLiteral(ExprLanguage::kRust, "r#", 0);
  EXPECT_FALSE(info);
  info = DoesBeginStringOrCharLiteral(ExprLanguage::kRust, "r#### ", 0);
  EXPECT_FALSE(info);

  // Valid raw prefix.
  info = DoesBeginStringOrCharLiteral(ExprLanguage::kRust, "r#\"", 0);
  ASSERT_TRUE(info);
  EXPECT_TRUE(info->is_raw);
  EXPECT_EQ("#", info->raw_marker);
  EXPECT_EQ(0u, info->string_begin);
  EXPECT_EQ(3u, info->contents_begin);

  info = DoesBeginStringOrCharLiteral(ExprLanguage::kRust, "  r####\" hello", 2);
  ASSERT_TRUE(info);
  EXPECT_TRUE(info->is_raw);
  EXPECT_EQ("####", info->raw_marker);
  EXPECT_EQ(2u, info->string_begin);
  EXPECT_EQ(8u, info->contents_begin);
}

TEST(ParseString, EscapedC) {
  size_t cur = 0;
  size_t error_location = 1234;
  ErrOr<std::string> result =
      Parse(ExprLanguage::kC, R"("some\rescaped\n")", &cur, &error_location);
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ("some\rescaped\n", result.value());

  // Unterminated string.
  cur = 0;
  result = Parse(ExprLanguage::kC, "\"something", &cur, &error_location);
  ASSERT_TRUE(result.has_error());
  EXPECT_EQ(0u, error_location);
  EXPECT_EQ("Hit end of input before the end of the string.", result.err().msg());

  // C-specific silliness.
  cur = 0;
  result = Parse(ExprLanguage::kC, R"("a\f\b\v ")", &cur, &error_location);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ("a\f\b\v ", result.value());

  // Hex sequences. We truncate overlong hex sequences ("\x1234" here) to the low 8 bits.
  cur = 0;
  result = Parse(ExprLanguage::kC, R"("\x01zed \x0x1 \x1234 \x1")", &cur, &error_location);
  ASSERT_TRUE(result.ok()) << result.err().msg();
  // The output contains a null so we have to construct manually.
  std::string expected("\x01zed ");
  expected.push_back(0);
  expected += "x1 \x34 \x01";
  EXPECT_EQ(expected, result.value());

  // Octal sequences.
  cur = 0;
  result = Parse(ExprLanguage::kC, R"("\019 \0\1 \1234 \1")", &cur, &error_location);
  ASSERT_TRUE(result.ok()) << result.err().msg();
  expected = "\x01";
  expected.append("9 ");
  expected.push_back(0);
  expected.push_back(1);
  expected += " \x9c \x01";  // 0x1234 = 0x29c, we truncate to the low bits to get 0x9c.
  EXPECT_EQ(expected, result.value());

  // Unicode escape sequences are unimplemented.
  cur = 0;
  result = Parse(ExprLanguage::kC, R"("\u1234")", &cur, &error_location);
  ASSERT_TRUE(result.has_error());
  EXPECT_EQ("Unicode escape sequences are not supported.", result.err().msg());
}

TEST(ParseString, EscapedRust) {
  size_t cur = 0;
  size_t error_location = 1234;
  ErrOr<std::string> result =
      Parse(ExprLanguage::kRust, R"("some\rescaped\n")", &cur, &error_location);
  ASSERT_TRUE(result.ok()) << result.err().msg();
  EXPECT_EQ("some\rescaped\n", result.value());

  // Unterminated string.
  cur = 0;
  result = Parse(ExprLanguage::kRust, R"("\x1)", &cur, &error_location);
  ASSERT_TRUE(result.has_error());
  EXPECT_EQ(3u, error_location);
  EXPECT_EQ("Expecting two hex digits.", result.err().msg());

  // Rust-specific escapes (\0 is a null).
  cur = 0;
  result = Parse(ExprLanguage::kRust, R"("foo\01bar")", &cur, &error_location);
  ASSERT_TRUE(result.ok()) << result.err().msg();
  std::string expected("foo");
  expected.push_back(0);
  expected += "1bar";
  EXPECT_EQ(expected, result.value());

  // Hex sequence that's not two digits.
  cur = 0;
  result = Parse(ExprLanguage::kRust, R"("\x1z)", &cur, &error_location);
  ASSERT_TRUE(result.has_error());
  EXPECT_EQ(3u, error_location);
  EXPECT_EQ("Expecting two hex digits.", result.err().msg());

  // Hex sequences. All Rust hex sequences are two digits so "\x1234" -> "\x12" + "34"
  cur = 0;
  result = Parse(ExprLanguage::kRust, R"("\x01zed \x00x1 \x1234 \x01")", &cur, &error_location);
  ASSERT_TRUE(result.ok()) << result.err().msg();
  // The output contains a null so we have to construct manually.
  expected = "\x01zed ";
  expected.push_back(0);
  expected += "x1 \x12";
  expected += "34 \x01";
  EXPECT_EQ(expected, result.value());

  // Unicode escape sequences are unimplemented.
  cur = 0;
  result = Parse(ExprLanguage::kRust, R"("\u{1234}")", &cur, &error_location);
  ASSERT_TRUE(result.has_error());
  EXPECT_EQ("Unicode escape sequences are not supported.", result.err().msg());
}

TEST(ParseString, RawC) {
  // Unterminated.
  size_t cur = 0;
  size_t error_location = 1234;
  ErrOr<std::string> result = Parse(ExprLanguage::kC, "R\"(", &cur, &error_location);
  ASSERT_TRUE(result.has_error());
  EXPECT_EQ("Hit end of input before the end of the string.", result.err().msg());
  EXPECT_EQ(0u, error_location);

  // Empty.
  cur = 0;
  result = Parse(ExprLanguage::kC, "R\"()\"", &cur, &error_location);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(5u, cur);
  EXPECT_EQ("", result.value());

  // Raw string with good ending and various escaped and weird characters.
  cur = 2;
  result = Parse(ExprLanguage::kC, "  R\"(hello\" world \\x10 \n)\"  ", &cur, &error_location);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(26u, cur);
  EXPECT_EQ("hello\" world \\x10 \n", result.value());

  // Raw string with delimiter.
  cur = 0;
  result = Parse(ExprLanguage::kC, "R\"foo(foo)\"foo)foo\"  ", &cur, &error_location);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(19u, cur);
  EXPECT_EQ("foo)\"foo", result.value());
}

TEST(ParseString, RawRust) {
  // Unterminated.
  size_t cur = 0;
  size_t error_location = 1234;
  ErrOr<std::string> result = Parse(ExprLanguage::kRust, "r#\"", &cur, &error_location);
  ASSERT_TRUE(result.has_error());
  EXPECT_EQ("Hit end of input before the end of the string.", result.err().msg());
  EXPECT_EQ(0u, error_location);

  // Empty.
  cur = 0;
  result = Parse(ExprLanguage::kRust, "r#\"\"#", &cur, &error_location);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(5u, cur);
  EXPECT_EQ("", result.value());

  // Raw string with good ending and various escaped and weird characters.
  cur = 2;
  result = Parse(ExprLanguage::kRust, "  r#\"hello\" world \\x10 \n\"#  ", &cur, &error_location);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(26u, cur);
  EXPECT_EQ("hello\" world \\x10 \n", result.value());

  // Raw string with delimiter.
  cur = 0;
  result = Parse(ExprLanguage::kRust, "r##\"#\"#\"##  ", &cur, &error_location);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(10u, cur);
  EXPECT_EQ("#\"#", result.value());
}

// Tests that Rust lifetimes are not counted as characters.
TEST(ParseString, RustLifetime) {
  EXPECT_TRUE(DoesBeginRustLifetime("'foo"));
  EXPECT_TRUE(DoesBeginRustLifetime("'bar "));
  EXPECT_TRUE(DoesBeginRustLifetime("'bar, 'something"));
  EXPECT_TRUE(DoesBeginRustLifetime("'A"));
  EXPECT_FALSE(DoesBeginRustLifetime("'A'"));
  EXPECT_FALSE(DoesBeginRustLifetime("'bar'"));
  EXPECT_FALSE(DoesBeginRustLifetime("'\\u1234'"));

  EXPECT_EQ(std::nullopt, DoesBeginStringOrCharLiteral(ExprLanguage::kRust, "'foo", 0));
  EXPECT_NE(std::nullopt, DoesBeginStringOrCharLiteral(ExprLanguage::kRust, "'foo' ", 0));
}

TEST(ParseString, Char) {
  // Normal character begin.
  std::optional<StringOrCharLiteralBegin> info =
      DoesBeginStringOrCharLiteral(ExprLanguage::kC, "'a'", 0);
  EXPECT_TRUE(info);
  EXPECT_EQ(ExprTokenType::kCharLiteral, info->token_type);
  EXPECT_FALSE(info->is_raw);
  EXPECT_EQ(0u, info->string_begin);
  EXPECT_EQ(1u, info->contents_begin);

  // Normal character parse.
  size_t cur = 0;
  size_t error_location = 1234;
  ErrOr<std::string> result = Parse(ExprLanguage::kRust, "'a'", &cur, &error_location);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(3u, cur);
  EXPECT_EQ("a", result.value());

  // Escaped character.
  cur = 0;
  result = Parse(ExprLanguage::kRust, R"('\n')", &cur, &error_location);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(4u, cur);
  EXPECT_EQ("\n", result.value());

  // Hex escaped character.
  cur = 0;
  result = Parse(ExprLanguage::kC, R"('\x01')", &cur, &error_location);
  ASSERT_TRUE(result.ok());
  EXPECT_EQ(6u, cur);
  EXPECT_EQ("\x01", result.value());

  // Too long character.
  cur = 0;
  result = Parse(ExprLanguage::kC, R"('abc')", &cur, &error_location);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(0u, cur);
  EXPECT_EQ(4u, error_location);
  EXPECT_EQ("Too much data in character literal.", result.err().msg());

  // Unterminated character.
  cur = 0;
  result = Parse(ExprLanguage::kC, R"('a)", &cur, &error_location);
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(0u, cur);
  EXPECT_EQ(0u, error_location);
  EXPECT_EQ("Hit end of input before the end of the character literal.", result.err().msg());
}

}  // namespace zxdb

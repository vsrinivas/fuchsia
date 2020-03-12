// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/parse_special_identifier.h"

#include "gtest/gtest.h"

namespace zxdb {

TEST(ParseSpecialIdentifier, NoName) {
  size_t cur = 0;
  SpecialIdentifier si;
  std::string contents;
  size_t error_location = 0;

  // Followed by end of input.
  Err err = ParseSpecialIdentifier("$", &cur, &si, &contents, &error_location);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Expected special name or '(' for escaped input.", err.msg());
  EXPECT_EQ(1u, error_location);

  // Followed by invalid character.
  cur = 1;
  error_location = 0;
  err = ParseSpecialIdentifier(" $ something", &cur, &si, &contents, &error_location);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Expected special name or '(' for escaped input.", err.msg());
  EXPECT_EQ(2u, error_location);

  // Followed by ().
  cur = 0;
  error_location = 0;
  err = ParseSpecialIdentifier("$()", &cur, &si, &contents, &error_location);
  EXPECT_TRUE(err.ok()) << err.msg();
  EXPECT_EQ(3u, cur);
  EXPECT_EQ(SpecialIdentifier::kEscaped, si);
  EXPECT_TRUE(contents.empty());

  // Followed by (something).
  cur = 0;
  error_location = 0;
  err = ParseSpecialIdentifier("$(something)", &cur, &si, &contents, &error_location);
  EXPECT_TRUE(err.ok()) << err.msg();
  EXPECT_EQ(12u, cur);
  EXPECT_EQ(SpecialIdentifier::kEscaped, si);
  EXPECT_EQ("something", contents);
}

TEST(ParseSpecialIdentifier, Name) {
  size_t cur = 0;
  SpecialIdentifier si;
  std::string contents;
  size_t error_location = 0;

  // Space terminates the name.
  Err err = ParseSpecialIdentifier("$main ", &cur, &si, &contents, &error_location);
  EXPECT_TRUE(err.ok()) << err.msg();
  EXPECT_EQ(5u, cur);
  EXPECT_EQ(SpecialIdentifier::kMain, si);
  EXPECT_EQ("", contents);

  // End of input terminates the name.
  cur = 0;
  err = ParseSpecialIdentifier("$anon", &cur, &si, &contents, &error_location);
  EXPECT_TRUE(err.ok()) << err.msg();
  EXPECT_EQ(5u, cur);
  EXPECT_EQ(SpecialIdentifier::kAnon, si);
  EXPECT_EQ("", contents);

  // Name with empty contents.
  cur = 0;
  err = ParseSpecialIdentifier("$reg()", &cur, &si, &contents, &error_location);
  EXPECT_TRUE(err.ok()) << err.msg();
  EXPECT_EQ(6u, cur);
  EXPECT_EQ(SpecialIdentifier::kRegister, si);
  EXPECT_EQ("", contents);

  // Name with nonempty contents.
  cur = 0;
  err = ParseSpecialIdentifier("$reg(foo)", &cur, &si, &contents, &error_location);
  EXPECT_TRUE(err.ok()) << err.msg();
  EXPECT_EQ(9u, cur);
  EXPECT_EQ(SpecialIdentifier::kRegister, si);
  EXPECT_EQ("foo", contents);

  // Invalid name.
  cur = 0;
  err = ParseSpecialIdentifier("$invalid", &cur, &si, &contents, &error_location);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ(8u, cur);
  EXPECT_EQ(SpecialIdentifier::kNone, si);
  EXPECT_TRUE(contents.empty());
}

TEST(ParseSpecialIdentifier, ContentsEscaping) {
  size_t cur = 0;
  SpecialIdentifier si;
  std::string contents;
  size_t error_location = 0;

  // Unterminated paren.
  Err err = ParseSpecialIdentifier("$(unterm", &cur, &si, &contents, &error_location);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Unexpected end of input in special identifier to match.", err.msg());
  EXPECT_EQ(1u, error_location);

  // Mismatched paren.
  cur = 0;
  err = ParseSpecialIdentifier("$(unt(erm)", &cur, &si, &contents, &error_location);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Unexpected end of input in special identifier to match.", err.msg());
  EXPECT_EQ(1u, error_location);

  // Escaped the opening and closing parens.
  cur = 0;
  err = ParseSpecialIdentifier("$(ab\\)c\\(de)", &cur, &si, &contents, &error_location);
  EXPECT_TRUE(err.ok());
  EXPECT_EQ(12u, cur);
  EXPECT_EQ(SpecialIdentifier::kEscaped, si);
  EXPECT_EQ("ab)c(de", contents);

  // Escaped backslash.
  cur = 0;
  err = ParseSpecialIdentifier("$(\\\\)", &cur, &si, &contents, &error_location);
  EXPECT_TRUE(err.ok());
  EXPECT_EQ(5u, cur);
  EXPECT_EQ(SpecialIdentifier::kEscaped, si);
  EXPECT_EQ("\\", contents);

  // Backslash at end of input.
  cur = 0;
  err = ParseSpecialIdentifier("$(\\", &cur, &si, &contents, &error_location);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Unexpected end of input in special identifier to match.", err.msg());
  EXPECT_EQ(1u, error_location);

  // Invalid escaped thing.
  cur = 0;
  err = ParseSpecialIdentifier("$(\\ab)", &cur, &si, &contents, &error_location);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ(SpecialIdentifier::kEscaped, si);
  EXPECT_EQ("Invalid backslash-escaped character in special identifier.", err.msg());
  EXPECT_EQ(3u, error_location);

  // Valid nested parens.
  cur = 0;
  err = ParseSpecialIdentifier("$((foo(bar)))", &cur, &si, &contents, &error_location);
  EXPECT_TRUE(err.ok());
  EXPECT_EQ(13u, cur);
  EXPECT_EQ(SpecialIdentifier::kEscaped, si);
  EXPECT_EQ("(foo(bar))", contents);
}

}  // namespace zxdb

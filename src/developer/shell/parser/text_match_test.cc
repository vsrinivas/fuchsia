// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/text_match.h"

#include <gtest/gtest.h>

#include "src/developer/shell/parser/ast_test.h"

namespace shell::parser {

TEST(TextMatchTest, TokenSingle) {
  const char* kTestString = "bob";

  ParseResult result = Token("bob")(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(3u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('bob')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(TextMatchTest, TokenMulti) {
  const char* kTestString = "bobbob";

  ParseResult result = Token("bob")(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(3u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('bob')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(TextMatchTest, TokenAmongJunk) {
  const char* kTestString = "##bob#!#bob#";

  ParseResult result = Token("bob")(ParseResult(kTestString));
  ASSERT_FALSE(result);
}

TEST(TextMatchTest, AnyChar) {
  const char* kTestString = "b";

  ParseResult result = AnyChar("abc")(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(1u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('b')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(TextMatchTest, AnyCharMulti) {
  const char* kTestString = "bc";

  ParseResult result = AnyChar("abc")(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(1u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('b')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(TextMatchTest, AnyCharAmongJunk) {
  const char* kTestString = "##b#!#c#";

  ParseResult result = AnyChar("abc")(ParseResult(kTestString));
  ASSERT_FALSE(result);
}

TEST(TextMatchTest, AnyCharBut) {
  const char* kTestString = "b";

  ParseResult result = AnyCharBut("123#!")(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(1u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('b')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(TextMatchTest, AnyCharButMulti) {
  const char* kTestString = "bc";

  ParseResult result = AnyCharBut("123#!")(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(1u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('b')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(TextMatchTest, AnyCharButAmongJunk) {
  const char* kTestString = "##b#!#c#";

  ParseResult result = AnyCharBut("123#!")(ParseResult(kTestString));
  ASSERT_FALSE(result);
}

TEST(TextMatchTest, CharGroup) {
  const char* kTestString = "b";

  ParseResult result = CharGroup("a-c")(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(1u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('b')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(TextMatchTest, CharGroupMulti) {
  const char* kTestString = "bc";

  ParseResult result = CharGroup("a-c")(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(1u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('b')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(TextMatchTest, CharGroupAmongJunk) {
  const char* kTestString = "##b#!#c#";

  ParseResult result = CharGroup("a-c")(ParseResult(kTestString));
  ASSERT_FALSE(result);
}

}  // namespace shell::parser

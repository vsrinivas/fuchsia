// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/text_match.h"

#include "gtest/gtest.h"
#include "src/developer/shell/parser/ast_test.h"

namespace shell::parser {

TEST(TextMatchTest, TokenSingle) {
  const char* kTestString = "bob";

  ParseResultStream match = Token("bob")(kTestString);
  EXPECT_TRUE(match.ok());

  ParseResult result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(3u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('bob')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  EXPECT_FALSE(result);
}

TEST(TextMatchTest, TokenMulti) {
  const char* kTestString = "bobbob";

  ParseResultStream match = Token("bob")(kTestString);
  EXPECT_TRUE(match.ok());

  ParseResult result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(3u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('bob')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  EXPECT_FALSE(result);
}

TEST(TextMatchTest, TokenAmongJunk) {
  const char* kTestString = "##bob#!#bob#";

  ParseResultStream match = Token("bob")(kTestString);
  EXPECT_FALSE(match.ok());

  ParseResult result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(5u, result.offset());
  EXPECT_EQ(2u, result.error_score());
  EXPECT_EQ("(E[Unexpected '##'] 'bob')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(0u, result.offset());
  EXPECT_EQ(3u, result.error_score());
  EXPECT_EQ("(E[Expected 'bob'])", result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  EXPECT_FALSE(result);
}

TEST(TextMatchTest, AnyChar) {
  const char* kTestString = "b";

  ParseResultStream match = AnyChar("letter", "abc")(kTestString);
  EXPECT_TRUE(match.ok());

  ParseResult result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(1u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('b')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  EXPECT_FALSE(result);
}

TEST(TextMatchTest, AnyCharMulti) {
  const char* kTestString = "bc";

  ParseResultStream match = AnyChar("letter", "abc")(kTestString);
  EXPECT_TRUE(match.ok());

  ParseResult result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(1u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('b')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  EXPECT_FALSE(result);
}

TEST(TextMatchTest, AnyCharAmongJunk) {
  const char* kTestString = "##b#!#c#";

  ParseResultStream match = AnyChar("letter", "abc")(kTestString);
  EXPECT_FALSE(match.ok());

  ParseResult result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(0u, result.offset());
  EXPECT_EQ(1u, result.error_score());
  EXPECT_EQ("(E[Expected letter])", result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(3u, result.offset());
  EXPECT_EQ(2u, result.error_score());
  EXPECT_EQ("(E[Unexpected '##'] 'b')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  EXPECT_FALSE(result);
}

TEST(TextMatchTest, AnyCharBut) {
  const char* kTestString = "b";

  ParseResultStream match = AnyCharBut("non-numeric", "123#!")(kTestString);
  EXPECT_TRUE(match.ok());

  ParseResult result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(1u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('b')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  EXPECT_FALSE(result);
}

TEST(TextMatchTest, AnyCharButMulti) {
  const char* kTestString = "bc";

  ParseResultStream match = AnyCharBut("non-numeric", "123#!")(kTestString);
  EXPECT_TRUE(match.ok());

  ParseResult result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(1u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('b')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  EXPECT_FALSE(result);
}

TEST(TextMatchTest, AnyCharButAmongJunk) {
  const char* kTestString = "##b#!#c#";

  ParseResultStream match = AnyCharBut("non-numeric", "123#!")(kTestString);
  EXPECT_FALSE(match.ok());

  ParseResult result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(0u, result.offset());
  EXPECT_EQ(1u, result.error_score());
  EXPECT_EQ("(E[Expected non-numeric])",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(3u, result.offset());
  EXPECT_EQ(2u, result.error_score());
  EXPECT_EQ("(E[Unexpected '##'] 'b')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  EXPECT_FALSE(result);
}

TEST(TextMatchTest, CharGroup) {
  const char* kTestString = "b";

  ParseResultStream match = CharGroup("letter", "a-c")(kTestString);
  EXPECT_TRUE(match.ok());

  ParseResult result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(1u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('b')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  EXPECT_FALSE(result);
}

TEST(TextMatchTest, CharGroupMulti) {
  const char* kTestString = "bc";

  ParseResultStream match = CharGroup("letter", "a-c")(kTestString);
  EXPECT_TRUE(match.ok());

  ParseResult result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(1u, result.offset());
  EXPECT_EQ(0u, result.error_score());
  EXPECT_EQ("('b')", result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  EXPECT_FALSE(result);
}

TEST(TextMatchTest, CharGroupAmongJunk) {
  const char* kTestString = "##b#!#c#";

  ParseResultStream match = CharGroup("letter", "a-c")(kTestString);
  EXPECT_FALSE(match.ok());

  ParseResult result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(0u, result.offset());
  EXPECT_EQ(1u, result.error_score());
  EXPECT_EQ("(E[Expected letter])", result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  ASSERT_TRUE(result);
  EXPECT_EQ(3u, result.offset());
  EXPECT_EQ(2u, result.error_score());
  EXPECT_EQ("(E[Unexpected '##'] 'b')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));

  result = match.Next();
  EXPECT_FALSE(result);
}

}  // namespace shell::parser

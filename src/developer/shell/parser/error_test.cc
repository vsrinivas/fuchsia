// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/error.h"

#include <gtest/gtest.h>

#include "src/developer/shell/parser/ast_test.h"
#include "src/developer/shell/parser/combinators.h"
#include "src/developer/shell/parser/text_match.h"

namespace shell::parser {

TEST(ErrorTest, Insert) {
  const char* kTestString = "smith";

  auto result =
      Seq(Alt(Token("bob"), ErInsert("Expected bob")), Token("smith"))(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(5u, result.offset());
  EXPECT_EQ(1u, result.errors());
  EXPECT_EQ("(E[Expected bob] 'smith')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(ErrorTest, Skip) {
  const char* kTestString = "bobsmith";

  auto result = Seq(Alt(Token("jed"), ErSkip("Unexpected bob", Token("bob"))),
                    Token("smith"))(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(8u, result.offset());
  EXPECT_EQ(1u, result.errors());
  EXPECT_EQ("(E[Unexpected bob] 'smith')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(ErrorTest, SkipMatchMacro) {
  const char* kTestString = "jedbobsmith";

  auto result = Seq(Alt(Token("fred"), ErSkip("Unexpected '%MATCH%'", OnePlus(AnyCharBut("s")))),
                    Token("smith"))(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(11u, result.offset());
  EXPECT_EQ(1u, result.errors());
  EXPECT_EQ("(E[Unexpected 'jedbob'] 'smith')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(ErrorTest, SeqReconnect) {
  const char* kTestString = "jedsmith";

  auto result = Seq(Alt(Seq(Token("jed"), Alt(Token("fred"), ErInsert("Expected fred"))), Empty),
                    Alt(Token("smith"), ErInsert("Expected smith")))(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(8u, result.offset());
  EXPECT_EQ(1u, result.errors());
  EXPECT_EQ("('jed' E[Expected fred] 'smith')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(ErrorTest, SeqReconnectThruToken) {
  const char* kTestString = "jedsmith";

  auto result =
      Seq(Token(Alt(Seq(Token("jed"), Alt(Token("fred"), ErInsert("Expected fred"))), Empty)),
          Alt(Token("smith"), ErInsert("Expected smith")))(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(8u, result.offset());
  EXPECT_EQ(1u, result.errors());
  EXPECT_EQ("(('jed' E[Expected fred]) 'smith')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(ErrorTest, SeqReconnectFromNull) {
  const char* kTestString = "jedbobsmith";

  auto result = Seq(
      Alt(Seq(Token("jed"), Alt(Token("fred"), ErSkip("Expected fred, got %MATCH%", Token("bob")))),
          Empty),
      Alt(Token("smith"), ErInsert("Expected smith")))(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(11u, result.offset());
  EXPECT_EQ(1u, result.errors());
  EXPECT_EQ("('jed' E[Expected fred, got bob] 'smith')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(ErrorTest, SeqReconnectMultipleAlt) {
  const char* kTestString = "bobbobjeb";

  auto result = Seq(Alt(Seq(Token("bob"), ErInsert("Expected smith")),
                        Seq(Token("bob"), Token("bob"), ErInsert("Expected smith")), Empty),
                    Token("jeb"))(ParseResult(kTestString));
  ASSERT_TRUE(result);
  EXPECT_EQ(9u, result.offset());
  EXPECT_EQ(1u, result.errors());
  EXPECT_EQ("('bob' 'bob' E[Expected smith] 'jeb')",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(ErrorTest, SeqReconnectLAssoc) {
  const char* kTestString = "a+a++a";

  auto result = Seq(
      LAssoc<ast::TestNode>(Token("a"), Seq(Token("+"), Alt(Token("a"), ErInsert("Expected a")))),
      EOS)(ParseResult(kTestString));

  ASSERT_TRUE(result);
  EXPECT_EQ(6u, result.offset());
  EXPECT_EQ(1u, result.errors());
  EXPECT_EQ("(((('a' '+' 'a') '+' E[Expected a]) '+' 'a'))",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

TEST(ErrorTest, SeqReconnectThroughNT) {
  const char* kTestString = "a>!a";

  auto result =
      Seq(Maybe(Token("!")),
          NT<ast::TestNode>(LAssoc<ast::TestNode>(
              Token("a"), Seq(Token(">"), Alt(Token("a"), ErSkip("! can't occur here",
                                                                 Seq(Token("!"), Token("a"))))))),
          EOS)(ParseResult(kTestString));

  ASSERT_TRUE(result);
  EXPECT_EQ(4u, result.offset());
  EXPECT_EQ(1u, result.errors());
  EXPECT_EQ("((('a' '>' E[! can't occur here])))",
            result.Reduce<ast::TestNode>().node()->ToString(kTestString));
}

}  // namespace shell::parser

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/parser.h"

#include "gtest/gtest.h"
#include "src/developer/shell/parser/ast.h"

namespace shell::parser {

TEST(ParserTest, VariableDecl) {
  const auto kTestString = "var s = 0";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ("Program(VariableDecl('var' Identifier('s') '=' Expression('0')))",
            parse->ToString(kTestString));
}

TEST(ParserTest, VariableDeclFail) {
  const auto kTestString = "vars = 0";

  auto parse = Parse(kTestString);
  EXPECT_TRUE(parse->HasErrors());

  EXPECT_EQ("Program(E[Unexpected 'vars = 0'])", parse->ToString(kTestString));
}

TEST(ParserTest, TwoVariableDecl) {
  const auto kTestString =
      "var x = 0;\n"
      "var y = 0";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('x') '=' Expression('0')) ';' VariableDecl('var' "
      "Identifier('y') '=' Expression('0')))",
      parse->ToString(kTestString));
}

TEST(ParserTest, TwoVariableDeclFail) {
  const auto kTestString =
      "varx = 0;\n"
      "var y = 0";

  auto parse = Parse(kTestString);
  EXPECT_TRUE(parse->HasErrors());

  EXPECT_EQ("Program(E[Unexpected 'varx = 0;\nvar y = 0'])", parse->ToString(kTestString));
}

TEST(ParserTest, TwoVariableDeclTrailingChars) {
  const auto kTestString =
      "var x = 0;\n"
      "var y = 0;\n"
      "xxx";

  auto parse = Parse(kTestString);
  EXPECT_TRUE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('x') '=' Expression('0')) ';' VariableDecl('var' "
      "Identifier('y') '=' Expression('0')) ';' E[Unexpected 'xxx'])",
      parse->ToString(kTestString));
}

TEST(ParserTest, TwoVariableDeclConst) {
  const auto kTestString =
      "var x = 0;\n"
      "const y = 0";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program("
      "VariableDecl('var' Identifier('x') '=' Expression('0')) ';' "
      "VariableDecl('const' Identifier('y') '=' Expression('0')))",
      parse->ToString(kTestString));
}

}  // namespace shell::parser

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/parser.h"

#include <tuple>

#include "gtest/gtest.h"
#include "src/developer/shell/parser/ast.h"

namespace shell::parser {
namespace checks {

class Skip {
 public:
  void Check(ast::Node* node) const {}
};

template <typename T>
class Expression {
 public:
  Expression(T checker) : checker_(std::move(checker)) {}
  void Check(ast::Node* node) const {
    auto expr = node->AsExpression();
    ASSERT_TRUE(expr);

    ASSERT_GT(expr->Children().size(), 0u);
    checker_.Check(expr->Children()[0].get());
  }

 private:
  T checker_;
};

class Integer {
 public:
  Integer(uint64_t value) : value_(value) {}
  void Check(ast::Node* node) const {
    auto integer = node->AsInteger();
    ASSERT_TRUE(integer);

    EXPECT_EQ(value_, integer->value());
  }

 private:
  uint64_t value_;
};

class String {
 public:
  String(std::string value) : value_(value) {}
  void Check(ast::Node* node) const {
    auto str = node->AsString();
    ASSERT_TRUE(str);

    EXPECT_EQ(value_, str->value());
  }

 private:
  std::string value_;
};

template <typename T>
class VariableDecl {
 public:
  VariableDecl(const std::string& name, bool is_const, T expr_check)
      : name_(name), is_const_(is_const), expr_check_(std::move(expr_check)) {}
  void Check(ast::Node* node) const {
    auto decl = node->AsVariableDecl();
    ASSERT_TRUE(decl);

    EXPECT_EQ(name_, decl->identifier());
    EXPECT_EQ(is_const_, decl->is_const());

    auto expr = decl->expression();
    ASSERT_TRUE(expr);

    expr_check_.Check(expr);
  }

 private:
  std::string name_;
  bool is_const_;
  T expr_check_;
};

template <typename... Args>
class Object {
 public:
  Object(Args... args) : fields_(args...) {}

  void Check(ast::Node* node) const {
    auto obj = node->AsObject();
    ASSERT_EQ(obj->fields().size(), std::tuple_size<std::tuple<Args...>>::value);
    if constexpr (std::tuple_size<std::tuple<Args...>>::value > 0) {
      DoCheck(obj);
    }
  }

 private:
  template <size_t I = 0>
  void DoCheck(ast::Object* obj) const {
    std::get<I>(fields_).Check(obj->fields()[I]);

    if constexpr ((I + 1) < std::tuple_size<std::tuple<Args...>>::value) {
      DoCheck<I + 1>(obj);
    }
  }

  std::tuple<Args...> fields_;
};

template <typename T>
class Field {
 public:
  Field(const std::string& name, T value_check) : name_(name), value_check_(value_check) {}

  void Check(ast::Node* node) const {
    auto field = node->AsField();
    ASSERT_TRUE(field);
    EXPECT_EQ(name_, field->name());
    ASSERT_TRUE(field->value());
    value_check_.Check(field->value());
  }

 private:
  std::string name_;
  T value_check_;
};

template <typename... Args>
class Program {
 public:
  Program(Args... args) : checks_(args...) {}

  void Check(ast::Node* node) const {
    ASSERT_GE(node->Children().size(), std::tuple_size<std::tuple<Args...>>::value);
    if constexpr (std::tuple_size<std::tuple<Args...>>::value > 0) {
      DoCheck(node);
    }
  }

 private:
  template <size_t I = 0>
  void DoCheck(ast::Node* node) const {
    std::get<I>(checks_).Check(node->Children()[I].get());

    if constexpr ((I + 1) < std::tuple_size<std::tuple<Args...>>::value) {
      DoCheck<I + 1>(node);
    }
  }

  std::tuple<Args...> checks_;
};

}  // namespace checks

#define CHECK_NODE(node, check) \
  do {                          \
    using namespace checks;     \
    check.Check(node.get());    \
    if (HasFatalFailure())      \
      return;                   \
  } while (0);

TEST(ParserTest, VariableDecl) {
  const auto kTestString = "var s = 0";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ("Program(VariableDecl('var' Identifier('s') '=' Expression(Integer('0'))))",
            parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("s", false, Expression(Integer(0)))));
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
      "Program(VariableDecl('var' Identifier('x') '=' Expression(Integer('0'))) ';' "
      "VariableDecl('var' Identifier('y') '=' Expression(Integer('0'))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("x", false, Expression(Integer(0))), Skip(),
                            VariableDecl("y", false, Expression(Integer(0)))));
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
      "Program(VariableDecl('var' Identifier('x') '=' Expression(Integer('0'))) ';' "
      "VariableDecl('var' Identifier('y') '=' Expression(Integer('0'))) ';' E[Unexpected 'xxx'])",
      parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("x", false, Expression(Integer(0))), Skip(),
                            VariableDecl("y", false, Expression(Integer(0)))));
}

TEST(ParserTest, TwoVariableDeclConst) {
  const auto kTestString =
      "var x = 0;\n"
      "const y = 0";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program("
      "VariableDecl('var' Identifier('x') '=' Expression(Integer('0'))) ';' "
      "VariableDecl('const' Identifier('y') '=' Expression(Integer('0'))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("x", false, Expression(Integer(0))), Skip(),
                            VariableDecl("y", true, Expression(Integer(0)))));
}

TEST(ParserTest, VariableDeclLongerInteger) {
  const auto kTestString = "var s = 12345";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ("Program(VariableDecl('var' Identifier('s') '=' Expression(Integer('12345'))))",
            parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("s", false, Expression(Integer(12345)))));
}

TEST(ParserTest, VariableDeclGroupedInteger) {
  const auto kTestString = "var s = 12_345";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ("Program(VariableDecl('var' Identifier('s') '=' Expression(Integer('12' '_' '345'))))",
            parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("s", false, Expression(Integer(12345)))));
}

TEST(ParserTest, VariableDeclHexInteger) {
  const auto kTestString = "var s = 0xabfF0912";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ("Program(VariableDecl('var' Identifier('s') '=' Expression(Integer('0x' 'abfF0912'))))",
            parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("s", false, Expression(Integer(0xabfF0912)))));
}

TEST(ParserTest, VariableDeclGroupedHexInteger) {
  const auto kTestString = "var s = 0xabfF_0912";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('s') '=' "
      "Expression(Integer('0x' 'abfF' '_' '0912'))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("s", false, Expression(Integer(0xabfF0912)))));
}

TEST(ParserTest, VariableDeclIntegerBadGroup) {
  const auto kTestString = "var s = _0912";

  auto parse = Parse(kTestString);
  EXPECT_TRUE(parse->HasErrors());

  EXPECT_EQ("Program(E[Unexpected 'var s = _0912'])", parse->ToString(kTestString));
}

TEST(ParserTest, VariableDeclIntegerZeroFirst) {
  const auto kTestString = "var s = 0912";

  auto parse = Parse(kTestString);
  EXPECT_TRUE(parse->HasErrors());

  EXPECT_EQ("Program(E[Unexpected 'var s = 0912'])", parse->ToString(kTestString));
}

TEST(ParserTest, VariableDeclIntegerHexNoMark) {
  const auto kTestString = "var s = 0abc";

  auto parse = Parse(kTestString);
  EXPECT_TRUE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('s') '=' "
      "Expression(Integer('0'))) E[Unexpected 'abc'])",
      parse->ToString(kTestString));
}

TEST(ParserTest, VariableDeclString) {
  const auto kTestString = R"(var s = "bob")";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(R"(Program(VariableDecl('var' Identifier('s') '=' Expression(String('"' 'bob' '"')))))",
            parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("s", false, Expression(String("bob")))));
}

TEST(ParserTest, VariableDeclStringEscapes) {
  const auto kTestString = R"(var s = "bob\"\n\r\t")";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(R"(Program(VariableDecl('var' Identifier('s') '=' )"
            R"(Expression(String('"' 'bob' '\"' '\n' '\r' '\t' '"')))))",
            parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("s", false, Expression(String("bob\"\n\r\t")))));
}

TEST(ParserTest, VariableDeclStringUtf8) {
  const auto kTestString = R"(var s = "Karkat ♋ \u00264b")";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(R"(Program(VariableDecl('var' Identifier('s') '=' )"
            R"(Expression(String('"' 'Karkat ♋ ' '\u00264b' '"')))))",
            parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("s", false, Expression(String("Karkat ♋ ♋")))));
}

TEST(ParserTest, VariableDeclStringLinebreak) {
  const auto kTestString =
      "var s = \"bob\\\n"
      "smith\"";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(R"(Program(VariableDecl('var' Identifier('s') '=' )"
            R"(Expression(String('"' 'bob' )"
            "'\\\n'"
            R"( 'smith' '"')))))",
            parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("s", false, Expression(String("bob\nsmith")))));
}

TEST(ParserTest, VariableDeclStringDangling) {
  const auto kTestString = "var s = \"bob";

  auto parse = Parse(kTestString);
  EXPECT_TRUE(parse->HasErrors());

  EXPECT_EQ("Program(E[Unexpected 'var s = \"bob'])", parse->ToString(kTestString));
}

TEST(ParserTest, VariableDeclObject) {
  const auto kTestString = "var s = { foo: 6 }";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('s') '=' "
      "Expression(Object('{' Field(Identifier('foo') ':' Integer('6')) '}'))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse,
             Program(VariableDecl("s", false, Expression(Object(Field("foo", Integer(6)))))));
}

TEST(ParserTest, VariableDeclObjectQuotedKey) {
  const auto kTestString = "var s = { \"foo\": 6 }";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('s') '=' "
      "Expression(Object('{' Field(String('\"' 'foo' '\"') ':' Integer('6')) '}'))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse,
             Program(VariableDecl("s", false, Expression(Object(Field("foo", Integer(6)))))));
}

TEST(ParserTest, VariableDeclObjectTrailingComma) {
  const auto kTestString = "var s = { foo: 6, }";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('s') '=' "
      "Expression(Object('{' Field(Identifier('foo') ':' Integer('6')) ',' '}'))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse,
             Program(VariableDecl("s", false, Expression(Object(Field("foo", Integer(6)))))));
}

TEST(ParserTest, VariableDeclObjectNested) {
  const auto kTestString = "var s = { foo: { bar: 7 } }";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('s') '=' "
      "Expression(Object('{' Field(Identifier('foo') ':' "
      "Object('{' Field(Identifier('bar') ':' Integer('7')) '}')) '}'))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse,
             Program(VariableDecl(
                 "s", false, Expression(Object(Field("foo", Object(Field("bar", Integer(7)))))))));
}

TEST(ParserTest, VariableDeclObjectMultiKey) {
  const auto kTestString = "var s = { foo: { bar: 7 }, baz: 23, bang: \"hiiii\" }";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('s') '=' "
      "Expression(Object('{' Field(Identifier('foo') ':' "
      "Object('{' Field(Identifier('bar') ':' Integer('7')) '}')) ',' "
      "Field(Identifier('baz') ':' Integer('23')) ',' "
      "Field(Identifier('bang') ':' String('\"' 'hiiii' '\"')) '}'))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse,
             Program(VariableDecl(
                 "s", false,
                 Expression(Object(Field("foo", Object(Field("bar", Integer(7)))),
                                   Field("baz", Integer(23)), Field("bang", String("hiiii")))))));
}

TEST(ParserTest, VariableDeclObjectEmpty) {
  const auto kTestString = "var s = {}";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ("Program(VariableDecl('var' Identifier('s') '=' Expression(Object('{' '}'))))",
            parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("s", false, Expression(Object()))));
}

TEST(ParserTest, VariableDeclObjectDangling) {
  const auto kTestString = "var s = { foo: { bar: 7 }, baz: 23, bang: \"hiiii\"";

  auto parse = Parse(kTestString);
  EXPECT_TRUE(parse->HasErrors());

  EXPECT_EQ("Program(E[Unexpected 'var s = { foo: { bar: 7 }, baz: 23, bang: \"hiiii\"'])",
            parse->ToString(kTestString));
}

TEST(ParserTest, VariableDeclObjectDanglingField) {
  const auto kTestString = "var s = { foo: ";

  auto parse = Parse(kTestString);
  EXPECT_TRUE(parse->HasErrors());

  EXPECT_EQ("Program(E[Unexpected 'var s = { foo: '])", parse->ToString(kTestString));
}

TEST(ParserTest, VariableDeclObjectNoFieldSeparator) {
  const auto kTestString = "var s = { foo 6 }";

  auto parse = Parse(kTestString);
  EXPECT_TRUE(parse->HasErrors());

  EXPECT_EQ("Program(E[Unexpected 'var s = { foo 6 }'])", parse->ToString(kTestString));
}

TEST(ParserTest, VariableDeclStringBadEscape) {
  const auto kTestString = "var s = \"bob\\qbob\"";

  auto parse = Parse(kTestString);
  EXPECT_TRUE(parse->HasErrors());

  EXPECT_EQ("Program(E[Unexpected 'var s = \"bob\\qbob\"'])", parse->ToString(kTestString));
}

}  // namespace shell::parser

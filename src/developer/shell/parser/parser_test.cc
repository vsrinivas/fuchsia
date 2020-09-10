// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/parser.h"

#include <tuple>

#include <gtest/gtest.h>

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

class Identifier {
 public:
  Identifier(std::string identifier) : identifier_(identifier) {}
  void Check(ast::Node* node) const {
    auto ident = node->AsIdentifier();
    ASSERT_TRUE(ident);

    EXPECT_EQ(identifier_, ident->identifier());
  }

 private:
  std::string identifier_;
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
    ASSERT_TRUE(obj);
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

class Path {
 public:
  Path(bool is_local, std::initializer_list<std::string> elements)
      : is_local_(is_local), elements_(elements) {}

  void Check(ast::Node* node) const {
    auto path = node->AsPath();
    ASSERT_TRUE(path);
    if (is_local_) {
      EXPECT_TRUE(path->is_local());
    } else {
      EXPECT_FALSE(path->is_local());
    }

    EXPECT_EQ(elements_.size(), path->elements().size());

    for (size_t i = 0; i < std::min(elements_.size(), path->elements().size()); i++) {
      EXPECT_EQ(elements_[i], path->elements()[i]);
    }
  }

 private:
  bool is_local_;
  std::vector<std::string> elements_;
};

template <typename A, typename B>
class AddSub {
 public:
  AddSub(A a, char op, B b) : op_(op), a_(std::move(a)), b_(std::move(b)) {}

  void Check(ast::Node* node) const {
    ASSERT_TRUE(op_ == '+' || op_ == '-') << "Operator for AddSub should be + or -";
    auto add_sub = node->AsAddSub();
    ASSERT_TRUE(add_sub);

    if (op_ == '+') {
      EXPECT_EQ(add_sub->type(), ast::AddSub::kAdd);
    } else {
      EXPECT_EQ(add_sub->type(), ast::AddSub::kSubtract);
    }

    ASSERT_TRUE(add_sub->a());
    a_.Check(add_sub->a());
    ASSERT_TRUE(add_sub->b());
    b_.Check(add_sub->b());
  }

 private:
  char op_;
  A a_;
  B b_;
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
    SCOPED_TRACE("CHECK_NODE"); \
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

  EXPECT_EQ("Program(E[Unrecoverable parse error])", parse->ToString(kTestString));

  CHECK_NODE(parse, Program());
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

  EXPECT_EQ("Program(E[Unrecoverable parse error])", parse->ToString(kTestString));

  CHECK_NODE(parse, Program());
}

TEST(ParserTest, TwoVariableDeclTrailingChars) {
  const auto kTestString =
      "var x = 0;\n"
      "var y = 0;\n"
      "xxx";

  auto parse = Parse(kTestString);
  EXPECT_TRUE(parse->HasErrors());

  EXPECT_EQ("Program(E[Unrecoverable parse error])", parse->ToString(kTestString));

  CHECK_NODE(parse, Program());
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
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ("Program(VariableDecl('var' Identifier('s') '=' Expression(Identifier('_0912'))))",
            parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("s", false, Expression(Identifier("_0912")))));
}

TEST(ParserTest, VariableDeclIntegerZeroFirst) {
  const auto kTestString = "var s = 0912";

  auto parse = Parse(kTestString);
  EXPECT_TRUE(parse->HasErrors());

  EXPECT_EQ("Program(E[Unrecoverable parse error])", parse->ToString(kTestString));

  CHECK_NODE(parse, Program());
}

TEST(ParserTest, VariableDeclIntegerHexNoMark) {
  const auto kTestString = "var s = 0abc";

  auto parse = Parse(kTestString);
  EXPECT_TRUE(parse->HasErrors());

  EXPECT_EQ("Program(E[Unrecoverable parse error])", parse->ToString(kTestString));

  CHECK_NODE(parse, Program());
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

  EXPECT_EQ("Program(E[Unrecoverable parse error])", parse->ToString(kTestString));

  CHECK_NODE(parse, Program());
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

  EXPECT_EQ("Program(E[Unrecoverable parse error])", parse->ToString(kTestString));

  CHECK_NODE(parse, Program());
}

TEST(ParserTest, VariableDeclObjectDanglingField) {
  const auto kTestString = "var s = { foo: ";

  auto parse = Parse(kTestString);
  EXPECT_TRUE(parse->HasErrors());

  EXPECT_EQ("Program(E[Unrecoverable parse error])", parse->ToString(kTestString));

  CHECK_NODE(parse, Program());
}

TEST(ParserTest, VariableDeclObjectNoFieldSeparator) {
  const auto kTestString = "var s = { foo 6 }";

  auto parse = Parse(kTestString);
  EXPECT_TRUE(parse->HasErrors());

  EXPECT_EQ("Program(E[Unrecoverable parse error])", parse->ToString(kTestString));

  CHECK_NODE(parse, Program());
}

TEST(ParserTest, VariableDeclStringBadEscape) {
  const auto kTestString = "var s = \"bob\\qbob\"";

  auto parse = Parse(kTestString);
  EXPECT_TRUE(parse->HasErrors());

  EXPECT_EQ("Program(E[Unrecoverable parse error])", parse->ToString(kTestString));

  CHECK_NODE(parse, Program());
}

TEST(ParserTest, VariableDeclPath) {
  const auto kTestString = "var x = ./somewhere/else";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('x') '=' "
      "Expression(Path('.' '/' 'somewhere' '/' 'else'))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse,
             Program(VariableDecl("x", false, Expression(Path(true, {"somewhere", "else"})))));
}

TEST(ParserTest, VariableDeclRootPath) {
  const auto kTestString = "var x = /somewhere/else";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('x') '=' "
      "Expression(Path('/' 'somewhere' '/' 'else'))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse,
             Program(VariableDecl("x", false, Expression(Path(false, {"somewhere", "else"})))));
}

TEST(ParserTest, VariableDeclRootOnlyPath) {
  const auto kTestString = "var x = /";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ("Program(VariableDecl('var' Identifier('x') '=' Expression(Path('/'))))",
            parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("x", false, Expression(Path(false, {})))));
}

TEST(ParserTest, VariableDeclDotOnlyPath) {
  const auto kTestString = "var x = .";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ("Program(VariableDecl('var' Identifier('x') '=' Expression(Path('.'))))",
            parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("x", false, Expression(Path(true, {})))));
}

TEST(ParserTest, VariableDeclDotSlashPath) {
  const auto kTestString = "var x = ./";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ("Program(VariableDecl('var' Identifier('x') '=' Expression(Path('.' '/'))))",
            parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("x", false, Expression(Path(true, {})))));
}

TEST(ParserTest, VariableDeclTrailingSlashPath) {
  const auto kTestString = "var x = ./somewhere/else/";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('x') '=' "
      "Expression(Path('.' '/' 'somewhere' '/' 'else' '/'))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse,
             Program(VariableDecl("x", false, Expression(Path(true, {"somewhere", "else"})))));
}

TEST(ParserTest, VariableDeclPathEscape) {
  const auto kTestString = "var x = ./somew\\ here/else";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('x') '=' "
      "Expression(Path('.' '/' 'somew' '\\ ' 'here' '/' 'else'))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse,
             Program(VariableDecl("x", false, Expression(Path(true, {"somew here", "else"})))));
}

TEST(ParserTest, VariableDeclPathQuote) {
  const auto kTestString = "var x = ./somew` oo oo `here/else";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('x') '=' "
      "Expression(Path('.' '/' 'somew' '`' ' oo oo ' '`' 'here' '/' 'else'))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("x", false,
                                         Expression(Path(true, {"somew oo oo here", "else"})))));
}

TEST(ParserTest, VariableDeclPathDanglingQuote) {
  const auto kTestString = "var x = ./somew` oo oo ";

  auto parse = Parse(kTestString);
  EXPECT_TRUE(parse->HasErrors());

  EXPECT_EQ("Program(E[Unrecoverable parse error])", parse->ToString(kTestString));

  CHECK_NODE(parse, Program());
}

TEST(ParserTest, VariableDeclPathInObject) {
  const auto kTestString = "var x = { foo: ./somewhere/else }";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('x') '=' "
      "Expression(Object('{' Field(Identifier('foo') ':' "
      "Path('.' '/' 'somewhere' '/' 'else')) '}'))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse,
             Program(VariableDecl(
                 "x", false, Expression(Object(Field("foo", Path(true, {"somewhere", "else"})))))));
}

TEST(ParserTest, VariableDeclIdentifier) {
  const auto kTestString = "var s = bob";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ("Program(VariableDecl('var' Identifier('s') '=' Expression(Identifier('bob'))))",
            parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("s", false, Expression(Identifier("bob")))));
}

TEST(ParserTest, VariableDeclIdentifierInObject) {
  const auto kTestString = "var s = { foo: bob }";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('s') '=' "
      "Expression(Object('{' Field(Identifier('foo') ':' Identifier('bob')) '}'))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("s", false,
                                         Expression(Object(Field("foo", Identifier("bob")))))));
}

TEST(ParserTest, VariableDeclAdd) {
  const auto kTestString = "var s = 1 + 2";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('s') '=' "
      "Expression(AddSub(Integer('1') '+' Integer('2')))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse,
             Program(VariableDecl("s", false, Expression(AddSub(Integer(1), '+', Integer(2))))));
}

TEST(ParserTest, VariableDeclSubtract) {
  const auto kTestString = "var s = 1 - 2";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('s') '=' "
      "Expression(AddSub(Integer('1') '-' Integer('2')))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse,
             Program(VariableDecl("s", false, Expression(AddSub(Integer(1), '-', Integer(2))))));
}

TEST(ParserTest, VariableDeclAddStrings) {
  const auto kTestString = "var s = \"foo\" + \"bar\"";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('s') '=' "
      "Expression(AddSub(String('\"' 'foo' '\"') '+' String('\"' 'bar' '\"')))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("s", false,
                                         Expression(AddSub(String("foo"), '+', String("bar"))))));
}

TEST(ParserTest, VariableDeclAddStringInt) {
  const auto kTestString = "var s = \"foo\" + 2";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('s') '=' "
      "Expression(AddSub(String('\"' 'foo' '\"') '+' Integer('2')))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse,
             Program(VariableDecl("s", false, Expression(AddSub(String("foo"), '+', Integer(2))))));
}

TEST(ParserTest, VariableDeclAddIntString) {
  const auto kTestString = "var s = 2 + \"bar\"";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('s') '=' "
      "Expression(AddSub(Integer('2') '+' String('\"' 'bar' '\"')))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse,
             Program(VariableDecl("s", false, Expression(AddSub(Integer(2), '+', String("bar"))))));
}

TEST(ParserTest, VariableDeclAddObjectVariable) {
  const auto kTestString = "var s = foo + { bar: 7 }";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('s') '=' Expression(AddSub(Identifier('foo') '+' "
      "Object('{' Field(Identifier('bar') ':' Integer('7')) '}')))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("s", false,
                                         Expression(AddSub(Identifier("foo"), '+',
                                                           Object(Field("bar", Integer(7))))))));
}

TEST(ParserTest, VariableDeclAddVariableObject) {
  const auto kTestString = "var s = { bar: 7 } + foo";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('s') '=' "
      "Expression(AddSub(Object('{' Field(Identifier('bar') ':' Integer('7')) '}') '+' "
      "Identifier('foo')))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl("s", false,
                                         Expression(AddSub(Object(Field("bar", Integer(7))), '+',
                                                           Identifier("foo"))))));
}

TEST(ParserTest, VariableDeclAddSubtractChain) {
  const auto kTestString = "var s = 1 - 2 + 7";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('s') '=' "
      "Expression(AddSub(AddSub(Integer('1') '-' Integer('2')) '+' Integer('7')))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl(
                        "s", false,
                        Expression(AddSub(AddSub(Integer(1), '-', Integer(2)), '+', Integer(7))))));
}

TEST(ParserTest, VariableDeclAddSubtractChainInObject) {
  const auto kTestString = "var s = { foo: 1 - 2 + 7 }";

  auto parse = Parse(kTestString);
  EXPECT_FALSE(parse->HasErrors());

  EXPECT_EQ(
      "Program(VariableDecl('var' Identifier('s') '=' "
      "Expression(Object('{' Field(Identifier('foo') ':' "
      "AddSub(AddSub(Integer('1') '-' Integer('2')) '+' Integer('7'))) '}'))))",
      parse->ToString(kTestString));

  CHECK_NODE(parse, Program(VariableDecl(
                        "s", false,
                        Expression(Object(Field("foo", AddSub(AddSub(Integer(1), '-', Integer(2)),
                                                              '+', Integer(7))))))));
}

TEST(ParserTest, VariableDeclAddSubtractDangle) {
  const auto kTestString = "var s = 1 - 2 + ";

  auto parse = Parse(kTestString);
  EXPECT_TRUE(parse->HasErrors());

  EXPECT_EQ("Program(E[Unrecoverable parse error])", parse->ToString(kTestString));

  CHECK_NODE(parse, Program());
}

TEST(ParserTest, VariableDeclAddSubtractTogether) {
  const auto kTestString = "var s = 1 - + 2";

  auto parse = Parse(kTestString);
  EXPECT_TRUE(parse->HasErrors());

  EXPECT_EQ("Program(E[Unrecoverable parse error])", parse->ToString(kTestString));

  CHECK_NODE(parse, Program());
}

}  // namespace shell::parser

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/expr_parser.h"

#include <sstream>

#include <gtest/gtest.h>

#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/expr/expr_tokenizer.h"
#include "src/developer/debug/zxdb/expr/found_name.h"
#include "src/developer/debug/zxdb/expr/mock_eval_context.h"
#include "src/developer/debug/zxdb/symbols/collection.h"

namespace zxdb {

class ExprParserTest : public testing::Test {
 public:
  // Adds the built-in types.
  ExprParserTest() {
    // "Namespace" is a namespace.
    ParsedIdentifier namespace_ident(ParsedIdentifierComponent("Namespace"));
    eval_context().AddName(namespace_ident, FoundName(FoundName::kNamespace, namespace_ident));

    // "Type" is a class type.
    ParsedIdentifier type_ident(ParsedIdentifierComponent("Type"));
    auto type = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
    type->set_assigned_name("Type");
    eval_context().AddName(type_ident, FoundName(std::move(type)));

    // Bare "Template" is a template (in FindName terms, a template is the name of a template with
    // no template parameters, the fully specified thing is just a normal type -- see below).
    ParsedIdentifier templ_ident(ParsedIdentifierComponent("Template"));
    eval_context().AddName(templ_ident, FoundName(FoundName::kTemplate, templ_ident));

    // Add a "Template<int>" specialization which is just a class type.
    ParsedIdentifier templ_int_ident(ParsedIdentifierComponent("Template", {"int"}));
    auto templ_int_type = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
    templ_int_type->set_assigned_name("Template<int>");
    eval_context().AddName(templ_int_ident, FoundName(std::move(templ_int_type)));
  }

  MockEvalContext& eval_context() { return *eval_context_; }

  // Valid after Parse() is called.
  ExprParser& parser() { return *parser_; }

  // Include_context controls whether the EvalContext is passed into the parser to provide type
  // information. This is omitted for some cases where identifiers are parsed.
  fxl::RefPtr<ExprNode> Parse(const char* input, ExprLanguage lang = ExprLanguage::kC,
                              bool include_context = true) {
    parser_.reset();

    tokenizer_ = std::make_unique<ExprTokenizer>(input, lang);
    if (!tokenizer_->Tokenize()) {
      ADD_FAILURE() << "Tokenization failure: " << input;
      return nullptr;
    }

    eval_context_->set_language(lang);
    parser_ = std::make_unique<ExprParser>(tokenizer_->TakeTokens(), lang,
                                           include_context ? eval_context_ : nullptr);
    return parser_->ParseStandaloneExpression();
  }

  // Does the parse and returns the string dump of the structure.
  std::string GetParseString(const char* input, bool include_context = true) {
    return GetParseString(input, ExprLanguage::kC, include_context);
  }

  std::string GetParseString(const char* input, ExprLanguage lang, bool include_context = true) {
    auto root = Parse(input, lang, include_context);
    if (!root) {
      // Expect calls to this to parse successfully.
      if (parser_.get())
        ADD_FAILURE() << "Parse failure: " << parser_->err().msg();
      return std::string();
    }

    std::ostringstream out;
    root->Print(out, 0);
    return out.str();
  }

 private:
  std::unique_ptr<ExprTokenizer> tokenizer_;
  std::unique_ptr<ExprParser> parser_;
  fxl::RefPtr<MockEvalContext> eval_context_ = fxl::MakeRefCounted<MockEvalContext>();
};

TEST_F(ExprParserTest, Empty) {
  auto result = Parse("");
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected expression instead of end of input.", parser().err().msg());

  result = Parse(" ");
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected expression instead of end of input.", parser().err().msg());

  result = Parse("; ");
  ASSERT_FALSE(result);
  EXPECT_EQ("Empty expression not permitted here.", parser().err().msg());
}

TEST_F(ExprParserTest, Block) {
  EXPECT_EQ("BLOCK\n", GetParseString("{}"));
  EXPECT_EQ(
      "BLOCK\n"
      " LITERAL(1)\n",
      GetParseString("{ 1; }"));

  // Our blocks allow the final statement to omit the semicolon (like Rust) even in C because
  // it allows standalone expressions to be parsed in "block" mode (allowing multiple statements
  // separated by semicolons) while not requiring a semicolon in the standalone expression case
  // (you don't want to have to terminate every "print" command with a semicolon).
  EXPECT_EQ(
      "BLOCK\n"
      " LITERAL(1)\n",
      GetParseString("{ 1 }"));

  EXPECT_EQ(
      "BLOCK\n"
      " BINARY_OP(+)\n"
      "  LITERAL(1)\n"
      "  IDENTIFIER(\"n\")\n"
      " LITERAL(2)\n"
      " LITERAL(3)\n",
      GetParseString("{ 1+n;2;    \n3 ;}"));

  // Last Rust expression doesn't require a semicolon.
  EXPECT_EQ(
      "BLOCK\n"
      " LITERAL(1)\n",
      GetParseString("{1}", ExprLanguage::kRust));
  EXPECT_EQ(
      "BLOCK\n"
      " LITERAL(1)\n"
      " LITERAL(2)\n",
      GetParseString("{1;2}", ExprLanguage::kRust));

  // Duplicate statement separators.
  EXPECT_EQ(
      "BLOCK\n"
      " LITERAL(1)\n"
      " LITERAL(2)\n",
      GetParseString("{;1;;2;}", ExprLanguage::kRust));

  // Nested blocks.
  EXPECT_EQ(
      "BLOCK\n"
      " BLOCK\n"
      "  BLOCK\n"
      " LITERAL(1)\n"
      " LITERAL(2)\n"
      " BLOCK\n",
      GetParseString("{{{}};1;;2;{;}}", ExprLanguage::kRust));

  // Rust blocks as expressions. Our parser will currenly accept this in C as well but it will get
  // rejected during execution since blocks don't return anything.
  EXPECT_EQ(
      "BINARY_OP(=)\n"
      " IDENTIFIER(\"a\")\n"
      " BLOCK\n"
      "  LITERAL(1)\n"
      "  LITERAL(2)\n",
      GetParseString("a = {1;2}", ExprLanguage::kRust));

  // Missing terminating "}".
  auto result = Parse("{1;");
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected '}'. Hit the end of input instead.", parser().err().msg());

  // No separator between elements.
  result = Parse("{1 {}}");
  EXPECT_FALSE(result);
  EXPECT_EQ("Unexpected token, did you forget an operator or a semicolon?", parser().err().msg());
}

TEST_F(ExprParserTest, Identifier) {
  auto result = Parse("name");
  ASSERT_TRUE(result);

  const IdentifierExprNode* ident = result->AsIdentifier();
  ASSERT_TRUE(ident);
  EXPECT_EQ("name", ident->ident().GetFullName());
}

TEST_F(ExprParserTest, Dot) {
  auto result = Parse("base.member");
  ASSERT_TRUE(result);

  const MemberAccessExprNode* access = result->AsMemberAccess();
  ASSERT_TRUE(access);
  EXPECT_EQ(ExprTokenType::kDot, access->accessor().type());
  EXPECT_EQ(".", access->accessor().value());

  // Left side is the "base" identifier.
  const IdentifierExprNode* base = access->left()->AsIdentifier();
  ASSERT_TRUE(base);
  EXPECT_EQ("base", base->ident().GetFullName());

  // Member name.
  EXPECT_EQ("member", access->member().GetFullName());
}

TEST_F(ExprParserTest, DotNumber) {
  auto result = Parse("base.0", ExprLanguage::kRust);
  ASSERT_TRUE(result);

  const MemberAccessExprNode* access = result->AsMemberAccess();
  ASSERT_TRUE(access);
  EXPECT_EQ(ExprTokenType::kDot, access->accessor().type());
  EXPECT_EQ(".", access->accessor().value());

  // Left side is the "base" identifier.
  const IdentifierExprNode* base = access->left()->AsIdentifier();
  ASSERT_TRUE(base);
  EXPECT_EQ("base", base->ident().GetFullName());

  // Member name.
  EXPECT_EQ("0", access->member().GetFullName());
}

TEST_F(ExprParserTest, DotNumberNoHex) {
  auto result = Parse("base.0xA", ExprLanguage::kRust);
  ASSERT_FALSE(result);

  EXPECT_EQ("Expected identifier for right-hand-side of \".\".", parser().err().msg());
  EXPECT_EQ(4u, parser().error_token().byte_offset());
  EXPECT_EQ(".", parser().error_token().value());
}

TEST_F(ExprParserTest, DotNumberNoRust) {
  auto result = Parse("base.0");
  ASSERT_FALSE(result);

  // This is parsed as an identifier followed by a floating-point number.
  EXPECT_EQ("Unexpected input, did you forget an operator or a semicolon?", parser().err().msg());
  EXPECT_EQ(4u, parser().error_token().byte_offset());
  EXPECT_EQ(".0", parser().error_token().value());
}

TEST_F(ExprParserTest, AccessorAtEnd) {
  auto result = Parse("base. ");
  ASSERT_FALSE(result);

  EXPECT_EQ("Expected identifier for right-hand-side of \".\".", parser().err().msg());

  EXPECT_EQ(4u, parser().error_token().byte_offset());
  EXPECT_EQ(".", parser().error_token().value());
}

TEST_F(ExprParserTest, BadAccessorMemberName) {
  auto result = Parse("base->23");
  ASSERT_FALSE(result);

  EXPECT_EQ("Expected identifier for right-hand-side of \"->\".", parser().err().msg());

  // This error reports the "->" as the location, one could also imagine reporting the right-side
  // token (if any) instead.
  EXPECT_EQ(4u, parser().error_token().byte_offset());
  EXPECT_EQ("->", parser().error_token().value());
}

TEST_F(ExprParserTest, Arrow) {
  auto result = Parse("base->member");
  ASSERT_TRUE(result);

  const MemberAccessExprNode* access = result->AsMemberAccess();
  ASSERT_TRUE(access);
  EXPECT_EQ(ExprTokenType::kArrow, access->accessor().type());
  EXPECT_EQ("->", access->accessor().value());

  // Left side is the "base" identifier.
  const IdentifierExprNode* base = access->left()->AsIdentifier();
  ASSERT_TRUE(base);
  EXPECT_EQ("base", base->ident().GetFullName());

  // Member name.
  EXPECT_EQ("member", access->member().GetFullName());

  // Arrow with no name.
  result = Parse("base->");
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected identifier for right-hand-side of \"->\".", parser().err().msg());
}

TEST_F(ExprParserTest, NestedDotArrow) {
  // These should be left-associative so do the "." first, then the "->". When evaluating the tree,
  // one first evaluates the left side of the accessor, then the right, which is why it looks
  // backwards in the dump.
  EXPECT_EQ(
      "ACCESSOR(->)\n"
      " ACCESSOR(.)\n"
      "  IDENTIFIER(\"foo\")\n"
      "  bar\n"
      " baz\n",
      GetParseString("foo.bar->baz"));
}

TEST_F(ExprParserTest, UnexpectedInput) {
  auto result = Parse("foo 5");
  ASSERT_FALSE(result);

  EXPECT_EQ("Unexpected input, did you forget an operator or a semicolon?", parser().err().msg());
  EXPECT_EQ(4u, parser().error_token().byte_offset());
}

TEST_F(ExprParserTest, ArrayAccess) {
  EXPECT_EQ(
      "ARRAY_ACCESS\n"
      " ARRAY_ACCESS\n"
      "  ACCESSOR(->)\n"
      "   ACCESSOR(.)\n"
      "    IDENTIFIER(\"foo\")\n"
      "    bar\n"
      "   baz\n"
      "  LITERAL(34)\n"
      " IDENTIFIER(\"bar\")\n",
      GetParseString("foo.bar->baz[34][bar]"));

  // Empty array access is an error.
  auto result = Parse("foo[]");
  ASSERT_FALSE(result);
  EXPECT_EQ("Unexpected token ']'.", parser().err().msg());
}

TEST_F(ExprParserTest, DereferenceAndAddress) {
  EXPECT_EQ(
      "DEREFERENCE\n"
      " IDENTIFIER(\"foo\")\n",
      GetParseString("*foo"));

  EXPECT_EQ(
      "ADDRESS_OF\n"
      " IDENTIFIER(\"foo\")\n",
      GetParseString("&foo"));

  // "*" and "&" should be right-associative with respect to each other but lower precedence than ->
  // and [].
  EXPECT_EQ(
      "ADDRESS_OF\n"
      " DEREFERENCE\n"
      "  ADDRESS_OF\n"
      "   ARRAY_ACCESS\n"
      "    ACCESSOR(->)\n"
      "     IDENTIFIER(\"foo\")\n"
      "     bar\n"
      "    LITERAL(1)\n",
      GetParseString("&*&foo->bar[1]"));

  // "*" by itself is an error.
  auto result = Parse("*");
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected expression instead of end of input.", parser().err().msg());
  EXPECT_EQ(0u, parser().error_token().byte_offset());
}

// () should override the default precedence of other operators.
TEST_F(ExprParserTest, Parens) {
  EXPECT_EQ(
      "ADDRESS_OF\n"
      " ARRAY_ACCESS\n"
      "  DEREFERENCE\n"
      "   ACCESSOR(->)\n"
      "    ADDRESS_OF\n"
      "     IDENTIFIER(\"foo\")\n"
      "    bar\n"
      "  LITERAL(1)\n",
      GetParseString("(&(*(&foo)->bar)[1])"));
}

// This test covers that the extent of number tokens are identified properly. Actually converting
// the strings to integers should be covered by the number parser tests.
TEST_F(ExprParserTest, IntegerLiterals) {
  EXPECT_EQ("LITERAL(5)\n", GetParseString("5"));
  EXPECT_EQ("LITERAL(5ull)\n", GetParseString(" 5ull"));
  EXPECT_EQ("LITERAL(0xAbC)\n", GetParseString("0xAbC"));
  EXPECT_EQ("LITERAL(0o551)\n", GetParseString("  0o551 "));
  EXPECT_EQ("LITERAL(0123)\n", GetParseString("0123"));
}

// Similar to IntegerLiterals tests, this just covers basic integration.
TEST_F(ExprParserTest, StringLiterals) {
  EXPECT_EQ("LITERAL(foo)\n", GetParseString("\"foo\""));
  EXPECT_EQ("LITERAL(foo\"bar)\n", GetParseString("R\"(foo\"bar)\""));
  EXPECT_EQ(
      "BINARY_OP(+)\n"
      " LITERAL(hello)\n"
      " LITERAL(world)\n",
      GetParseString("  \"hello\" + \"world\""));
}

TEST_F(ExprParserTest, UnaryMath) {
  EXPECT_EQ(
      "UNARY(-)\n"
      " LITERAL(5)\n",
      GetParseString("-5"));
  EXPECT_EQ(
      "UNARY(-)\n"
      " LITERAL(5)\n",
      GetParseString(" - 5 "));

  EXPECT_EQ(
      "UNARY(!)\n"
      " IDENTIFIER(\"foo\")\n",
      GetParseString("!foo "));

  EXPECT_EQ(
      "UNARY(-)\n"
      " DEREFERENCE\n"
      "  IDENTIFIER(\"foo\")\n",
      GetParseString("-*foo"));

  EXPECT_EQ("IDENTIFIER(\"~foo\")\n", GetParseString("~foo"));
  EXPECT_EQ(
      "UNARY(~)\n"
      " LITERAL(21)\n",
      GetParseString("~21"));
  EXPECT_EQ(
      "UNARY(~)\n"
      " IDENTIFIER(\"foo\")\n",
      GetParseString("~ foo"));

  // "-" by itself is an error.
  auto result = Parse("-");
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected expression for '-'.", parser().err().msg());
  EXPECT_EQ(0u, parser().error_token().byte_offset());
}

TEST_F(ExprParserTest, AndOr) {
  EXPECT_EQ(
      "BINARY_OP(&&)\n"
      " BINARY_OP(&)\n"
      "  ADDRESS_OF\n"
      "   IDENTIFIER(\"a\")\n"
      "  ADDRESS_OF\n"
      "   IDENTIFIER(\"b\")\n"
      " BINARY_OP(&)\n"
      "  IDENTIFIER(\"c\")\n"
      "  IDENTIFIER(\"d\")\n",
      GetParseString("& a & & b && c & d"));
  EXPECT_EQ(
      "BINARY_OP(||)\n"
      " BINARY_OP(|)\n"
      "  IDENTIFIER(\"a\")\n"
      "  IDENTIFIER(\"b\")\n"
      " BINARY_OP(|)\n"
      "  IDENTIFIER(\"c\")\n"
      "  IDENTIFIER(\"d\")\n",
      GetParseString("a | b || c | d"));
  EXPECT_EQ(
      "BINARY_OP(||)\n"
      " BINARY_OP(&&)\n"
      "  IDENTIFIER(\"a\")\n"
      "  IDENTIFIER(\"b\")\n"
      " BINARY_OP(&&)\n"
      "  IDENTIFIER(\"c\")\n"
      "  IDENTIFIER(\"d\")\n",
      GetParseString("a && b || c && d"));
}

TEST_F(ExprParserTest, Identifiers) {
  EXPECT_EQ("IDENTIFIER(\"foo\")\n", GetParseString("foo", false));
  EXPECT_EQ("IDENTIFIER(::\"foo\")\n", GetParseString("::foo", false));
  EXPECT_EQ("IDENTIFIER(::\"foo\"; ::\"~bar\")\n", GetParseString("::foo :: ~bar", false));

  auto result = Parse("::");
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected name after '::'.", parser().err().msg());

  result = Parse(":: :: name");
  ASSERT_FALSE(result);
  EXPECT_EQ("Could not identify thing to the left of '::' as a type or namespace.",
            parser().err().msg());

  result = Parse("foo bar");
  ASSERT_FALSE(result);
  EXPECT_EQ("Unexpected identifier, did you forget an operator or a semicolon?",
            parser().err().msg());

  // It's valid to have identifiers with colons in them to access class members (this is how you
  // provide an explicit base class).
  /* TODO(brettw) convert an accessor to use an Identifier for the name so this
     test works.
  EXPECT_EQ(
      "ACCESSOR(->)\n"
      "  IDENTIFIER(\"foo\")\n",
      GetParseString("foo->Baz::bar"));
  */
}

TEST_F(ExprParserTest, SpecialIdentifiers) {
  // Most special identifier parsing is tested in the dedicated special identifier parser tests.
  // This checks that the integration is correct.
  //
  // These take no context (pass "false" to GetParseString()) to put the parser into identifier
  // mode.
  EXPECT_EQ("IDENTIFIER(\"ns\"; ::\"$anon\"; ::\"Foo\")\n",
            GetParseString("ns::$anon::Foo", false));
  EXPECT_EQ("IDENTIFIER(\"$main\")\n", GetParseString("$main", false));
  EXPECT_EQ(
      "BINARY_OP(+)\n"
      " BINARY_OP(+)\n"
      "  LITERAL(2)\n"
      "  IDENTIFIER(\"$plt(foo_bar)\")\n"
      " LITERAL(2)\n",
      GetParseString("2+$plt(foo_bar)+2"));

  // For escaping, the identifier won't contain the escaping characters, these are for parsing and
  // output only.
  EXPECT_EQ("IDENTIFIER(\"{{impl}}\"; ::\"some(crazyness)$here)\")\n",
            GetParseString("$({{impl}})::$(some(crazyness)$here\\))", false));
}

TEST_F(ExprParserTest, CppOperators) {
  EXPECT_EQ("IDENTIFIER(\"Class\"; ::\"operator>>\")\n",
            GetParseString("Class::operator>>", false));
  EXPECT_EQ("IDENTIFIER(\"Class\"; ::\"operator()\")\n",
            GetParseString("Class :: operator ()", false));
  EXPECT_EQ(
      "BINARY_OP(>)\n"
      " IDENTIFIER(\"Class\"; ::\"operator>>\")\n"
      " IDENTIFIER(\"operator<<\")\n",
      GetParseString("Class::operator>>>operator<<", false));

  // Type conversion operator. To parse this correctly both the class name and the destination
  // type name must be known as types to the parser.
  EXPECT_EQ("IDENTIFIER(\"Type\"; ::\"operator const Type*\")\n",
            GetParseString("Type::operator   const  Type *"));

  // We allow invalid operator names and just treat them as a literal "operator" identifier to allow
  // C variables using that name.
  EXPECT_EQ("IDENTIFIER(\"operator\")\n", GetParseString("operator"));
  EXPECT_EQ("IDENTIFIER(\"operator\")\n", GetParseString("(operator)"));
  EXPECT_EQ(
      "ACCESSOR(.)\n"
      " IDENTIFIER(\"foo\")\n"
      " operator\n",
      GetParseString("foo.operator"));
}

// Tests << and >> operators. They are challenging because this can also appear in template
// expressions and are parsed as something else.
TEST_F(ExprParserTest, Shift) {
  // This is parsed as (2 << 2) < (static_cast<Template<int>>(2) >> 2) > 2" with the < and > then
  // parsed left-to-right.
  EXPECT_EQ(
      "BINARY_OP(>)\n"
      " BINARY_OP(<)\n"
      "  BINARY_OP(<<)\n"
      "   LITERAL(2)\n"
      "   LITERAL(2)\n"
      "  BINARY_OP(>>)\n"
      "   CAST(static_cast)\n"
      "    TYPE(Template<int>)\n"
      "    LITERAL(2)\n"
      "   LITERAL(2)\n"
      " LITERAL(2)\n",
      GetParseString("2<<2<static_cast<Template<int>>(2)>>2>2"));

  EXPECT_EQ(
      "BINARY_OP(>>=)\n"
      " BINARY_OP(<<=)\n"
      "  IDENTIFIER(\"i\")\n"
      "  LITERAL(5)\n"
      " LITERAL(6)\n",
      GetParseString("i <<= 5 >>= 6"));

  // Some invalid shift combinations.
  auto result = Parse("a << = 2");
  ASSERT_FALSE(result);
  EXPECT_EQ("Unexpected token '='.", parser().err().msg());

  result = Parse("a > >= 2");
  ASSERT_FALSE(result);
  EXPECT_EQ("Unexpected token '>='.", parser().err().msg());

  result = Parse("a>>>2");
  ASSERT_FALSE(result);
  EXPECT_EQ("Unexpected token '>'.", parser().err().msg());
}

TEST_F(ExprParserTest, FunctionCall) {
  // Simple call with no args.
  EXPECT_EQ(
      "FUNCTIONCALL\n"
      " IDENTIFIER(\"Call\")\n",
      GetParseString("Call()"));

  // Scoped call with namespaces and templates.
  EXPECT_EQ(
      "FUNCTIONCALL\n"
      " IDENTIFIER(\"ns\"; ::\"Foo\",<\"int\">; ::\"GetCurrent\")\n",
      GetParseString("ns::Foo<int>::GetCurrent()", false));

  // One arg.
  EXPECT_EQ(
      "FUNCTIONCALL\n"
      " IDENTIFIER(\"Call\")\n"
      " LITERAL(42)\n",
      GetParseString("Call(42)"));

  // Several complex args and a nested call.
  EXPECT_EQ(
      "FUNCTIONCALL\n"
      " IDENTIFIER(\"Call\")\n"
      " IDENTIFIER(\"a\")\n"
      " BINARY_OP(=)\n"
      "  IDENTIFIER(\"b\")\n"
      "  LITERAL(5)\n"
      " FUNCTIONCALL\n"
      "  IDENTIFIER(\"OtherCall\")\n",
      GetParseString("Call(a, b = 5, OtherCall())"));

  // Function call on an object with ".".
  EXPECT_EQ(
      "FUNCTIONCALL\n"
      " ACCESSOR(.)\n"
      "  ACCESSOR(.)\n"
      "   IDENTIFIER(\"foo\")\n"
      "   bar\n"
      "  Baz\n"
      " IDENTIFIER(\"a\")\n",
      GetParseString("foo.bar.Baz(a)"));

  // Function call on nested stuff with "->".
  EXPECT_EQ(
      "FUNCTIONCALL\n"
      " ACCESSOR(->)\n"
      "  BINARY_OP(+)\n"
      "   IDENTIFIER(\"a\")\n"
      "   IDENTIFIER(\"b\")\n"
      "  Call\n",
      GetParseString("(a + b)-> Call()"));

  // Unmatched "(" error.
  auto result = Parse("Call(a, ");
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected expression instead of end of input.", parser().err().msg());

  // Arguments not separated by commas.
  result = Parse("Call(a b)");
  ASSERT_FALSE(result);
  EXPECT_EQ("Unexpected identifier, did you forget an operator or a semicolon?",
            parser().err().msg());

  // Empty parameter
  result = Parse("Call(a, , b)");
  ASSERT_FALSE(result);
  EXPECT_EQ("Unexpected token ','.", parser().err().msg());

  // Trailing comma.
  result = Parse("Call(a,)");
  ASSERT_FALSE(result);
  EXPECT_EQ("Unexpected token ')'.", parser().err().msg());

  // Thing on the left is not an identifier.
  result = Parse("5()");
  ASSERT_FALSE(result);
  EXPECT_EQ("Unexpected '('.", parser().err().msg());
}

// These pass no context (false) to GetParseString() so it goes into the mode where it prefers
// identifiers and doesn't try to look up anything in the symbol system.
TEST_F(ExprParserTest, Templates) {
  EXPECT_EQ(R"(IDENTIFIER("foo",<>))"
            "\n",
            GetParseString("foo<>", false));
  EXPECT_EQ(R"(IDENTIFIER("foo",<"Foo">))"
            "\n",
            GetParseString("foo<Foo>", false));
  EXPECT_EQ(R"(IDENTIFIER("foo",<"Foo", "5">))"
            "\n",
            GetParseString("foo< Foo,5 >", false));
  EXPECT_EQ(
      R"(IDENTIFIER("std"; ::"map",<"Key", "Value", "std::less<Key>", "std::allocator<std::pair<Key const, Value>>">; ::"insert"))"
      "\n",
      GetParseString("std::map<Key, Value, std::less<Key>, "
                     "std::allocator<std::pair<Key const, Value>>>::insert",
                     false));

  // Unmatched "<" error. This is generated by the parser because it's expecting the match for the
  // outer level.
  auto result = Parse("std::map<Key, Value", ExprLanguage::kC, false);
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected '>' to match. Hit the end of input instead.", parser().err().msg());

  // This unmatched token is generated by the template type skipper which is why the error message
  // is slightly different (both are OK).
  result = Parse("std::map<key[, value", ExprLanguage::kC, false);
  ASSERT_FALSE(result);
  EXPECT_EQ("Unmatched '['.", parser().err().msg());

  // Duplicate template spec. This will actually be parsed as "less than" and "greater than" and it
  // will look like "greater than" is missing the right-hand-side.
  result = Parse("Foo<Bar><Baz>", ExprLanguage::kC, false);
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected expression after '>'.", parser().err().msg());

  // Empty value.
  result = Parse("Foo<1,,2>", ExprLanguage::kC, false);
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected template parameter.", parser().err().msg());

  // Trailing comma.
  result = Parse("Foo<Bar,>", ExprLanguage::kC, false);
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected template parameter.", parser().err().msg());
}

TEST_F(ExprParserTest, BinaryOp) {
  EXPECT_EQ(
      "BINARY_OP(=)\n"
      " IDENTIFIER(\"a\")\n"
      " BINARY_OP(+)\n"
      "  LITERAL(23)\n"
      "  BINARY_OP(*)\n"
      "   IDENTIFIER(\"j\")\n"
      "   BINARY_OP(+)\n"
      "    IDENTIFIER(\"a\")\n"
      "    LITERAL(1)\n",
      GetParseString("a = 23 + j * (a + 1)"));
}

TEST_F(ExprParserTest, ArraySize) {
  // This is converting something to an array of size 3, getting a value out, and adding to it.
  EXPECT_EQ(
      "BINARY_OP(+)\n"
      " BINARY_OP(@)\n"
      "  IDENTIFIER(\"a\")\n"
      "  ARRAY_ACCESS\n"
      "   LITERAL(3)\n"
      "   LITERAL(1)\n"
      " LITERAL(2)\n",
      GetParseString("a@3[1] + 2"));

  EXPECT_EQ(
      "BINARY_OP(@)\n"
      " IDENTIFIER(\"a\")\n"
      " BINARY_OP(+)\n"
      "  IDENTIFIER(\"m\")\n"
      "  LITERAL(2)\n",
      GetParseString("a@(m+2)"));
}

TEST_F(ExprParserTest, Comparison) {
  // Passing no context (the "false" parameter) puts the parser into a mode where it prefers to
  // parse identifiers.
  EXPECT_EQ(
      "BINARY_OP(!=)\n"
      " IDENTIFIER(\"a\",<\"int\">; ::\"foo\")\n"
      " LITERAL(0)\n",
      GetParseString("a<int>::foo != 0", false));

  // Doing the same thing with symbol context will try to look up "a" as a name which will fail
  // so we won't think it's a template and get a completely different parsing.
  EXPECT_EQ(
      "BINARY_OP(!=)\n"
      " BINARY_OP(>)\n"
      "  BINARY_OP(<)\n"
      "   IDENTIFIER(\"a\")\n"
      "   TYPE(int)\n"
      "  IDENTIFIER(::\"foo\")\n"
      " LITERAL(0)\n",
      GetParseString("a<int>::foo != 0", true));

  EXPECT_EQ(
      "BINARY_OP(&&)\n"
      " BINARY_OP(<)\n"
      "  LITERAL(1)\n"
      "  LITERAL(2)\n"
      " BINARY_OP(>)\n"
      "  IDENTIFIER(\"a\")\n"
      "  LITERAL(4)\n",
      GetParseString("1 < 2 && a > 4"));

  EXPECT_EQ(
      "BINARY_OP(||)\n"
      " BINARY_OP(<=)\n"
      "  LITERAL(1)\n"
      "  LITERAL(2.3)\n"
      " BINARY_OP(>=)\n"
      "  IDENTIFIER(\"a\")\n"
      "  LITERAL(4e9f)\n",
      GetParseString("1 <= 2.3 || a >= 4e9f"));

  EXPECT_EQ(
      "BINARY_OP(<=>)\n"
      " IDENTIFIER(\"a\")\n"
      " LITERAL(4)\n",
      GetParseString("a <=> 4"));
}

// Tests parsing identifier names that require lookups from the symbol system.
TEST_F(ExprParserTest, NamesWithSymbolLookup) {
  // Bare namespace is an error.
  auto result = Parse("Namespace");
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected expression after namespace name.", parser().err().msg());

  // Bare template is an error.
  result = Parse("Template");
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected template args after template name.", parser().err().msg());

  // Nothing after "::"
  result = Parse("Namespace::");
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected name after '::'.", parser().err().msg());

  // Can't put a template on a type that's not a template.
  result = Parse("Type<int>");
  ASSERT_FALSE(result);
  // This error message might change with future type support because it might look like a
  // comparison between a type and an int.
  EXPECT_EQ("Template parameters not valid on this object type.", parser().err().msg());

  // Can't put a template on a namespace.
  result = Parse("Namespace<int>");
  ASSERT_FALSE(result);
  EXPECT_EQ("Template parameters not valid on this object type.", parser().err().msg());

  // Good type name.
  EXPECT_EQ(
      "FUNCTIONCALL\n"
      " IDENTIFIER(\"Namespace\"; ::\"Template\",<\"int\">; ::\"fn\")\n",
      GetParseString("Namespace::Template<int>::fn()", false));
}

TEST_F(ExprParserTest, TrueFalse) {
  EXPECT_EQ("LITERAL(true)\n", GetParseString("true"));
  EXPECT_EQ("LITERAL(false)\n", GetParseString("false"));
  EXPECT_EQ(
      "BINARY_OP(&&)\n"
      " LITERAL(false)\n"
      " LITERAL(true)\n",
      GetParseString("false&&true"));
}

TEST_F(ExprParserTest, Types) {
  EXPECT_EQ("TYPE(Type)\n", GetParseString("Type"));
  EXPECT_EQ("IDENTIFIER(\"NotType\")\n", GetParseString("NotType"));

  EXPECT_EQ("TYPE(const Type)\n", GetParseString("const Type"));
  EXPECT_EQ("TYPE(const Type)\n", GetParseString("Type const"));

  // It would be better it this printed as "const volatile Type" but our heuristic for moving
  // modifiers to the beginning isn't good enough.
  EXPECT_EQ("TYPE(volatile Type const)\n", GetParseString("const volatile Type"));

  // Duplicate const qualifications.
  auto result = Parse("const Type const");
  ASSERT_FALSE(result);
  EXPECT_EQ("Duplicate 'const' type qualification.", parser().err().msg());
  result = Parse("const const Type");
  ASSERT_FALSE(result);
  EXPECT_EQ("Duplicate 'const' type qualification.", parser().err().msg());

  EXPECT_EQ("TYPE(Type*)\n", GetParseString("Type*"));
  EXPECT_EQ("TYPE(Type**)\n", GetParseString("Type * *"));
  EXPECT_EQ("TYPE(Type&&)\n", GetParseString("Type &&"));
  EXPECT_EQ("TYPE(Type&**)\n", GetParseString("Type&**"));
  EXPECT_EQ("TYPE(volatile Type* restrict* const)\n",
            GetParseString("Type volatile *restrict* const"));

  // "const" should force us into type mode.
  result = Parse("const NonType");
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected a type name but could not find a type named 'NonType'.",
            parser().err().msg());

  // Try sizeof() with both a type and a non-type.
  EXPECT_EQ(
      "SIZEOF\n"
      " TYPE(Type*)\n",
      GetParseString("sizeof(Type*)"));
  EXPECT_EQ(
      "SIZEOF\n"
      " IDENTIFIER(\"foo\")\n",
      GetParseString("sizeof(foo)"));
}

TEST_F(ExprParserTest, C_Cast) {
  EXPECT_EQ(
      "CAST(C)\n"
      " TYPE(Type)\n"
      " IDENTIFIER(\"a\")\n",
      GetParseString("(Type)(a)"));

  EXPECT_EQ(
      "BINARY_OP(&&)\n"
      " CAST(C)\n"
      "  TYPE(Type*)\n"
      "  IDENTIFIER(\"a\")\n"
      " IDENTIFIER(\"b\")\n",
      GetParseString("(Type*)a && b"));

  EXPECT_EQ(
      "CAST(C)\n"
      " TYPE(Type)\n"
      " ACCESSOR(->)\n"
      "  ARRAY_ACCESS\n"
      "   IDENTIFIER(\"a\")\n"
      "   LITERAL(0)\n"
      "  b\n",
      GetParseString("(Type)a[0]->b"));

  // Looks like a cast but it's not a type.
  auto result = Parse("(NotType)a");
  EXPECT_FALSE(result);
  EXPECT_EQ("Unexpected input, did you forget an operator or a semicolon?", parser().err().msg());
}

TEST_F(ExprParserTest, RustCast) {
  EXPECT_EQ(
      "CAST(Rust)\n"
      " TYPE(Type)\n"
      " IDENTIFIER(\"a\")\n",
      GetParseString("a as Type", ExprLanguage::kRust));

  EXPECT_EQ(
      "BINARY_OP(&&)\n"
      " CAST(Rust)\n"
      "  TYPE(Type*)\n"
      "  IDENTIFIER(\"a\")\n"
      " IDENTIFIER(\"b\")\n",
      GetParseString("a as *Type && b", ExprLanguage::kRust));

  EXPECT_EQ(
      "BINARY_OP(&&)\n"
      " CAST(Rust)\n"
      "  TYPE(Type*******)\n"
      "  IDENTIFIER(\"a\")\n"
      " IDENTIFIER(\"b\")\n",
      GetParseString("a as &mut &mut && mut &*&Type && b", ExprLanguage::kRust));

  EXPECT_EQ(
      "CAST(Rust)\n"
      " TYPE(Type)\n"
      " ACCESSOR(->)\n"
      "  ARRAY_ACCESS\n"
      "   IDENTIFIER(\"a\")\n"
      "   LITERAL(0)\n"
      "  b\n",
      GetParseString("a[0]->b as Type", ExprLanguage::kRust));

  // We can't actually cast to tuple, so these wouldn't evaluate, but we want to test the type
  // parsing anyway.
  EXPECT_EQ(
      "CAST(Rust)\n"
      " TYPE((Type, Type, Type))\n"
      " IDENTIFIER(\"a\")\n",
      GetParseString("a as (Type, Type, Type)", ExprLanguage::kRust));

  EXPECT_EQ(
      "CAST(Rust)\n"
      " TYPE(())\n"
      " IDENTIFIER(\"a\")\n",
      GetParseString("a as ()", ExprLanguage::kRust));

  EXPECT_EQ(
      "CAST(Rust)\n"
      " TYPE((Type,))\n"
      " IDENTIFIER(\"a\")\n",
      GetParseString("a as (Type,)", ExprLanguage::kRust));

  EXPECT_EQ(
      "CAST(Rust)\n"
      " TYPE(Type)\n"
      " IDENTIFIER(\"a\")\n",
      GetParseString("a as (Type)", ExprLanguage::kRust));

  EXPECT_EQ(
      "CAST(Rust)\n"
      " TYPE(Type[23])\n"
      " IDENTIFIER(\"a\")\n",
      GetParseString("a as [Type; 23]", ExprLanguage::kRust));

  EXPECT_EQ(
      "CAST(Rust)\n"
      " TYPE(Type[])\n"
      " IDENTIFIER(\"a\")\n",
      GetParseString("a as [Type]", ExprLanguage::kRust));

  EXPECT_EQ(
      "CAST(Rust)\n"
      " TYPE(Type[23]*)\n"
      " IDENTIFIER(\"a\")\n",
      GetParseString("a as &[Type; 23]", ExprLanguage::kRust));

  // Looks like a cast but it's not a type.
  auto result = Parse("a as NotType", ExprLanguage::kRust);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected a type name but could not find a type named 'NotType'.",
            parser().err().msg());

  // Rust cast but we're not in rust
  result = Parse("a as Type", ExprLanguage::kC);
  EXPECT_FALSE(result);
  EXPECT_EQ("Unexpected identifier, did you forget an operator or a semicolon?",
            parser().err().msg());
}

TEST_F(ExprParserTest, BadRustArrays) {
  auto result = Parse("a as [NotType]", ExprLanguage::kRust);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected a type name but could not find a type named 'NotType'.",
            parser().err().msg());

  result = Parse("a as [NotType; 23]", ExprLanguage::kRust);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected a type name but could not find a type named 'NotType'.",
            parser().err().msg());

  result = Parse("a as [Type; 23", ExprLanguage::kRust);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected ']' before end of input.", parser().err().msg());

  result = Parse("a as [Type;", ExprLanguage::kRust);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected element count before end of input.", parser().err().msg());

  result = Parse("a as [Type", ExprLanguage::kRust);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected ']' before end of input.", parser().err().msg());
}

TEST_F(ExprParserTest, BadRustTuples) {
  auto result = Parse("a as (Type, NotType)", ExprLanguage::kRust);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected a type name but could not find a type named 'NotType'.",
            parser().err().msg());

  // Missing comma
  result = Parse("a as (Type Type)", ExprLanguage::kRust);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected ')' or ','.", parser().err().msg());

  // Missing end
  result = Parse("a as (Type, Type", ExprLanguage::kRust);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected ')' or ',' before end of input.", parser().err().msg());

  // Missing end with comma
  result = Parse("a as (Type,", ExprLanguage::kRust);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected ')' or ',' before end of input.", parser().err().msg());

  // Missing end, no groupings.
  result = Parse("a as (Type", ExprLanguage::kRust);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected ')' or ',' before end of input.", parser().err().msg());
}

TEST_F(ExprParserTest, CppCast) {
  EXPECT_EQ(
      "CAST(reinterpret_cast)\n"
      " TYPE(Type*)\n"
      " IDENTIFIER(\"a\")\n",
      GetParseString("reinterpret_cast<Type*>(a)"));

  EXPECT_EQ(
      "CAST(static_cast)\n"
      " TYPE(Type*)\n"
      " IDENTIFIER(\"a\")\n",
      GetParseString("static_cast<Type*>(a)"));

  EXPECT_EQ(
      "CAST(reinterpret_cast)\n"
      " TYPE(const Type&&)\n"
      " BINARY_OP(&&)\n"
      "  IDENTIFIER(\"x\")\n"
      "  IDENTIFIER(\"y\")\n",
      GetParseString("reinterpret_cast<  const Type&& >( x && y)"));

  auto result = Parse("reinterpret_cast<");
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected type name before end of input.", parser().err().msg());

  // Functional-style casts aren't currently supported. Make sure we report that properly.
  result = Parse("double(5)");
  EXPECT_FALSE(result);
  EXPECT_EQ("Functional-style casts are not currently supported.", parser().err().msg());

  result = Parse("Type()");
  EXPECT_FALSE(result);
  EXPECT_EQ("Functional-style casts are not currently supported.", parser().err().msg());
}

TEST_F(ExprParserTest, ParseIdentifier) {
  Err err;

  // Empty input.
  ParsedIdentifier empty_ident;
  err = ExprParser::ParseIdentifier("", &empty_ident);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Expected expression instead of end of input.", err.msg());
  EXPECT_EQ("", empty_ident.GetDebugName());

  // Normal word.
  ParsedIdentifier word_ident;
  err = ExprParser::ParseIdentifier("foo", &word_ident);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ("\"foo\"", word_ident.GetDebugName());

  // Destructor.
  ParsedIdentifier destr_ident;
  err = ExprParser::ParseIdentifier("Foo::~Foo", &destr_ident);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(R"("Foo"; ::"~Foo")", destr_ident.GetDebugName());

  // Complicated identifier (copied from STL).
  ParsedIdentifier complex_ident;
  err = ExprParser::ParseIdentifier(
      "std::unordered_map<"
      "std::__2::basic_string<char>, "
      "unsigned long, "
      "std::__2::hash<std::__2::basic_string<char> >, "
      "std::__2::equal_to<std::__2::basic_string<char> >, "
      "std::__2::allocator<std::__2::pair<const std::__2::basic_string<char>, "
      "unsigned long> >>",
      &complex_ident);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(
      "\"std\"; "
      "::"
      "\"unordered_map\","
      "<\"std::__2::basic_string<char>\", "
      "\"unsigned long\", "
      "\"std::__2::hash<std::__2::basic_string<char>>\", "
      "\"std::__2::equal_to<std::__2::basic_string<char>>\", "
      "\"std::__2::allocator<std::__2::pair<"
      "const std::__2::basic_string<char>, unsigned long>>\">",
      complex_ident.GetDebugName());
}

TEST_F(ExprParserTest, FromStringError) {
  // Error from input.
  Identifier bad_ident;
  Err err = ExprParser::ParseIdentifier("Foo<Bar", &bad_ident);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Expected '>' to match. Hit the end of input instead.", err.msg());
  EXPECT_EQ("", bad_ident.GetDebugName());
}

TEST_F(ExprParserTest, If) {
  EXPECT_EQ(
      "CONDITION\n"
      " IF\n"
      "  BINARY_OP(==)\n"
      "   IDENTIFIER(\"i\")\n"
      "   LITERAL(1)\n"
      " THEN\n"
      "  BINARY_OP(=)\n"
      "   IDENTIFIER(\"b\")\n"
      "   LITERAL(1)\n",
      GetParseString("if (i == 1) b = 1;"));

  EXPECT_EQ(
      "CONDITION\n"
      " IF\n"
      "  IDENTIFIER(\"i\")\n",
      GetParseString("if (i) ;"));

  EXPECT_EQ(
      "CONDITION\n"
      " IF\n"
      "  IDENTIFIER(\"i\")\n"
      " THEN\n"
      "  BLOCK\n",
      GetParseString("if (i) {} else ;"));

  EXPECT_EQ(
      "CONDITION\n"
      " IF\n"
      "  LITERAL(1)\n"
      " THEN\n"
      "  IDENTIFIER(\"foo\")\n"
      " ELSEIF\n"
      "  LITERAL(0)\n"
      " THEN\n"
      "  IDENTIFIER(\"bar\")\n",
      GetParseString("if (1) foo; else if (0) bar;"));

  // Rust with no parens is OK.
  EXPECT_EQ(
      "CONDITION\n"
      " IF\n"
      "  LITERAL(1)\n"
      " THEN\n"
      "  BLOCK\n"
      "   IDENTIFIER(\"foo\")\n"
      " ELSEIF\n"
      "  LITERAL(0)\n"
      " THEN\n"
      "  BLOCK\n"
      "   IDENTIFIER(\"bar\")\n",
      GetParseString("if 1 {foo} else if 0 {bar}", ExprLanguage::kRust));

  // C with no parens is a failure.
  auto result = Parse("if 1 foo; else if 0 bar;", ExprLanguage::kC);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected '(' for if.", parser().err().msg());

  // Rust requires {} for the blocks.
  result = Parse("if 1 foo;", ExprLanguage::kRust);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected '{'.", parser().err().msg());

  // Missing semicolons.
  result = Parse("if (1) foo else bar;", ExprLanguage::kC);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected ';'.", parser().err().msg());
  result = Parse("if (1) foo; else bar", ExprLanguage::kC);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected ';'. Hit the end of input instead.", parser().err().msg());
}

TEST_F(ExprParserTest, CTernaryIf) {
  EXPECT_EQ(
      "CONDITION\n"
      " IF\n"
      "  LITERAL(1)\n"
      " THEN\n"
      "  LITERAL(2)\n"
      " ELSE\n"
      "  LITERAL(3)\n",
      GetParseString("1 ? 2 : 3"));

  EXPECT_EQ(
      "CONDITION\n"
      " IF\n"
      "  BINARY_OP(==)\n"
      "   LITERAL(2)\n"
      "   BINARY_OP(-)\n"
      "    LITERAL(3)\n"
      "    LITERAL(1)\n"
      " THEN\n"
      "  CONDITION\n"
      "   IF\n"
      "    LITERAL(2)\n"
      "   THEN\n"
      "    LITERAL(1)\n"
      "   ELSE\n"
      "    IDENTIFIER(\"a\")\n"
      " ELSE\n"
      "  BINARY_OP(+)\n"
      "   LITERAL(3)\n"
      "   LITERAL(9)\n",
      GetParseString("2==3-1 ? 2?1:a : 3+9"));

  // Missing colon.
  auto result = Parse(" 3 ? 1");
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected ':' for previous '?'. Hit the end of input instead.", parser().err().msg());

  // Missing condition
  result = Parse("? a : b");
  EXPECT_FALSE(result);
  EXPECT_EQ("Unexpected token '?'.", parser().err().msg());

  // Not supported in Rust.
  result = Parse("a ? b : c", ExprLanguage::kRust);
  EXPECT_FALSE(result);
  EXPECT_EQ("Rust '?' operators are not supported.", parser().err().msg());
}

TEST_F(ExprParserTest, ForLoop) {
  EXPECT_EQ(
      "LOOP(for)\n"
      " ;\n"
      " ;\n"
      " ;\n"
      " ;\n"
      " BLOCK\n",
      GetParseString("for (;;) {}"));

  EXPECT_EQ(
      "LOOP(for)\n"
      " BINARY_OP(=)\n"
      "  IDENTIFIER(\"i\")\n"
      "  LITERAL(0)\n"
      " ;\n"
      " ;\n"
      " ;\n"
      " ;\n",
      GetParseString("for (i = 0;;);"));

  // Note: we can't write "i < 100" here because in this test environment there is no variable or
  // type information so the parser can't know what "i" is. When there is no such information, it
  // assumes the < is for a template (this behavior is important since the same parser is used for
  // parsing breakpoint names with no context; real expressions will have this context).
  EXPECT_EQ(
      "LOOP(for)\n"
      " IDENTIFIER(\"i\")\n"
      " BINARY_OP(<=)\n"
      "  IDENTIFIER(\"i\")\n"
      "  LITERAL(100)\n"
      " ;\n"
      " BINARY_OP(=)\n"
      "  IDENTIFIER(\"i\")\n"
      "  BINARY_OP(+)\n"
      "   IDENTIFIER(\"i\")\n"
      "   LITERAL(1)\n"
      " BLOCK\n"
      "  FUNCTIONCALL\n"
      "   IDENTIFIER(\"print\")\n"
      "   IDENTIFIER(\"i\")\n"
      "  FUNCTIONCALL\n"
      "   IDENTIFIER(\"beep\")\n",
      GetParseString("for (i; i <= 100; i = i + 1) {\n"
                     "  print(i);\n"
                     "  beep();\n"
                     "}"));

  // Variable declaration in the loop (this needs the name lookup to get the builtin types).
  EXPECT_EQ(
      "LOOP(for)\n"
      " LOCAL_VAR_DECL(i, 0)\n"
      "  size_t\n"
      "  LITERAL(0)\n"
      " BINARY_OP(<)\n"
      "  LOCAL_VAR(0)\n"
      "  LITERAL(100)\n"
      " ;\n"
      " BINARY_OP(=)\n"
      "  LOCAL_VAR(0)\n"
      "  BINARY_OP(+)\n"
      "   LOCAL_VAR(0)\n"
      "   LITERAL(1)\n"
      " BLOCK\n",
      GetParseString("for (size_t i = 0; i < 100; i = i + 1) {}"));

  // No loop body.
  auto result = Parse("for (;;)");
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected expression instead of end of input.", parser().err().msg());

  // Unterminated loop body.
  result = Parse("for (;;) j");
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected ';'. Hit the end of input instead.", parser().err().msg());

  // Not enough semicolons.
  result = Parse("for (i = 0;)");
  EXPECT_FALSE(result);
  EXPECT_EQ("Unexpected token ')'.", parser().err().msg());

  result = Parse("for (i = 0 i <= 100; i++)");
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected ';'.", parser().err().msg());

  // Missing "()"
  result = Parse("for i = 0 {}");
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected '(' for 'for' loop.", parser().err().msg());

  result = Parse("for (i = 0; i; j");
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected ')' to match. Hit the end of input instead.", parser().err().msg());
}

TEST_F(ExprParserTest, DoLoop) {
  EXPECT_EQ(
      "LOOP(do)\n"
      " ;\n"
      " ;\n"
      " LITERAL(true)\n"
      " ;\n"
      " BLOCK\n",
      GetParseString("do {} while (true);"));

  EXPECT_EQ(
      "LOOP(do)\n"
      " ;\n"
      " ;\n"
      " BINARY_OP(>=)\n"
      "  IDENTIFIER(\"i\")\n"
      "  LITERAL(0)\n"
      " ;\n"
      " BLOCK\n"
      "  FUNCTIONCALL\n"
      "   IDENTIFIER(\"print\")\n"
      "   IDENTIFIER(\"i\")\n",
      GetParseString("do { print(i); } while (i >= 0);"));

  // Semicolon loop body.
  auto result = Parse("do ; while (true);");
  EXPECT_FALSE(result);
  EXPECT_EQ("Empty expression not permitted here.", parser().err().msg());

  // No loop expression.
  result = Parse("do {} while();");
  EXPECT_FALSE(result);
  EXPECT_EQ("Unexpected token ')'.", parser().err().msg());
}

TEST_F(ExprParserTest, CWhileLoop) {
  EXPECT_EQ(
      "LOOP(while)\n"
      " ;\n"
      " LITERAL(true)\n"
      " ;\n"
      " ;\n"
      " BLOCK\n",
      GetParseString("while (true) {}"));

  EXPECT_EQ(
      "LOOP(while)\n"
      " ;\n"
      " BINARY_OP(>=)\n"
      "  IDENTIFIER(\"i\")\n"
      "  LITERAL(0)\n"
      " ;\n"
      " ;\n"
      " ;\n",
      GetParseString("while (i >= 0);"));

  EXPECT_EQ(
      "LOOP(while)\n"
      " ;\n"
      " IDENTIFIER(\"i\")\n"
      " ;\n"
      " ;\n"
      " BINARY_OP(=)\n"
      "  IDENTIFIER(\"i\")\n"
      "  BINARY_OP(+)\n"
      "   IDENTIFIER(\"i\")\n"
      "   LITERAL(1)\n",
      GetParseString("while (i) i = i + 1;"));

  EXPECT_EQ(
      "LOOP(while)\n"
      " ;\n"
      " IDENTIFIER(\"i\")\n"
      " ;\n"
      " ;\n"
      " BLOCK\n"
      "  FUNCTIONCALL\n"
      "   IDENTIFIER(\"print\")\n"
      "   IDENTIFIER(\"i\")\n"
      "  BINARY_OP(=)\n"
      "   IDENTIFIER(\"i\")\n"
      "   BINARY_OP(+)\n"
      "    IDENTIFIER(\"i\")\n"
      "    LITERAL(1)\n",
      GetParseString("while (i) { print(i); i = i + 1; }"));

  // No loop body.
  auto result = Parse("while (true)");
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected expression instead of end of input.", parser().err().msg());

  // Unterminated loop body.
  result = Parse("while (foo) j");
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected ';'. Hit the end of input instead.", parser().err().msg());

  // No loop expression.
  result = Parse("while () j;");
  EXPECT_FALSE(result);
  EXPECT_EQ("Unexpected token ')'.", parser().err().msg());

  // Semicolon loop expression
  result = Parse("while (i;) j;");
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected ')' to match.", parser().err().msg());

  // Break outside of loop.
  result = Parse("break;");
  EXPECT_FALSE(result);
  EXPECT_EQ("Use of 'break' in this context is not allowed.", parser().err().msg());
}

TEST_F(ExprParserTest, RustWhileLoop) {
  EXPECT_EQ(
      "LOOP(while)\n"
      " ;\n"
      " LITERAL(true)\n"
      " ;\n"
      " ;\n"
      " BLOCK\n",
      GetParseString("while true {}", ExprLanguage::kRust));

  EXPECT_EQ(
      "LOOP(while)\n"
      " ;\n"
      " BINARY_OP(>=)\n"
      "  IDENTIFIER(\"i\")\n"
      "  LITERAL(0)\n"
      " ;\n"
      " ;\n"
      " BLOCK\n"
      "  BINARY_OP(=)\n"
      "   IDENTIFIER(\"i\")\n"
      "   BINARY_OP(-)\n"
      "    IDENTIFIER(\"i\")\n"
      "    LITERAL(1)\n",
      GetParseString("while i >= 0 { i = i - 1; }", ExprLanguage::kRust));

  // Parens are still permitted, and this omits the semicolon for the last block statement.
  EXPECT_EQ(
      "LOOP(while)\n"
      " ;\n"
      " IDENTIFIER(\"i\")\n"
      " ;\n"
      " ;\n"
      " BLOCK\n"
      "  FUNCTIONCALL\n"
      "   IDENTIFIER(\"print\")\n"
      "   IDENTIFIER(\"i\")\n"
      "  BINARY_OP(=)\n"
      "   IDENTIFIER(\"i\")\n"
      "   BINARY_OP(+)\n"
      "    IDENTIFIER(\"i\")\n"
      "    LITERAL(1)\n"
      "  BREAK\n",
      GetParseString("while (i) { print(i); i = i + 1; break; }", ExprLanguage::kRust));

  // No loop body.
  auto result = Parse("while true", ExprLanguage::kRust);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected '{'. Hit the end of input instead.", parser().err().msg());

  // No {}.
  result = Parse("while foo j;", ExprLanguage::kRust);
  EXPECT_FALSE(result);
  EXPECT_EQ("Unexpected identifier, did you forget an operator or a semicolon?",
            parser().err().msg());
}

TEST_F(ExprParserTest, RustLoop) {
  EXPECT_EQ(
      "LOOP(loop)\n"
      " ;\n"
      " ;\n"
      " ;\n"
      " ;\n"
      " BLOCK\n",
      GetParseString("loop {}", ExprLanguage::kRust));

  EXPECT_EQ(
      "LOOP(loop)\n"
      " ;\n"
      " ;\n"
      " ;\n"
      " ;\n"
      " BLOCK\n"
      "  BINARY_OP(=)\n"
      "   IDENTIFIER(\"i\")\n"
      "   BINARY_OP(-)\n"
      "    IDENTIFIER(\"i\")\n"
      "    LITERAL(1)\n"
      "  BREAK\n",
      GetParseString("loop { i = i - 1; break; }", ExprLanguage::kRust));

  // Extra expression.
  auto result = Parse("loop true {}", ExprLanguage::kRust);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected '{'.", parser().err().msg());

  // Missing body.
  result = Parse("loop", ExprLanguage::kRust);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected '{'. Hit the end of input instead.", parser().err().msg());

  // Unterminated body.
  result = Parse("loop {", ExprLanguage::kRust);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected '}'. Hit the end of input instead.", parser().err().msg());
}

// Tests that comments are ignored.
TEST_F(ExprParserTest, Comments) {
  EXPECT_EQ(
      "BINARY_OP(=)\n"
      " IDENTIFIER(\"a\")\n"
      " BINARY_OP(+)\n"
      "  LITERAL(3)\n"
      "  LITERAL(2)\n",
      GetParseString("a = /* no */ 3 +// no\n 2"));

  // Unmatched */ token.
  auto result = Parse(" 3 + */");
  EXPECT_FALSE(result);
  EXPECT_EQ("Unexpected */", parser().err().msg());
}

TEST_F(ExprParserTest, CVariableDecl) {
  EXPECT_EQ(
      "LOCAL_VAR_DECL(i, 0)\n"
      " int\n"
      " BINARY_OP(+)\n"
      "  LITERAL(0)\n"
      "  LITERAL(76)\n",
      GetParseString("int i = 0 + 76;", ExprLanguage::kC));

  EXPECT_EQ(
      "LOCAL_VAR_DECL(d, 0)\n"
      " double\n"
      " ;\n",
      GetParseString("double d;", ExprLanguage::kC));

  EXPECT_EQ(
      "LOCAL_VAR_DECL(i, 0)\n"
      " <C++-style auto>\n"
      " LITERAL(76)\n",
      GetParseString("auto i = 76", ExprLanguage::kC));

  EXPECT_EQ(
      "LOCAL_VAR_DECL(i, 0)\n"
      " <C++-style auto&>\n"
      " IDENTIFIER(\"some_var\")\n",
      GetParseString("auto& i = some_var", ExprLanguage::kC));

  EXPECT_EQ(
      "LOCAL_VAR_DECL(i, 0)\n"
      " <C++-style auto*>\n"
      " ADDRESS_OF\n"
      "  IDENTIFIER(\"some_var\")\n",
      GetParseString("auto* i = &some_var;", ExprLanguage::kC));

  EXPECT_EQ(
      "LOCAL_VAR_DECL(v, 0)\n"
      " Type**\n"
      " ;\n",
      GetParseString("Type** v;", ExprLanguage::kC));

  // Paren initialization.
  EXPECT_EQ(
      "LOCAL_VAR_DECL(v, 0)\n"
      " Type**\n"
      " LITERAL(0)\n",
      GetParseString("Type** v(0);", ExprLanguage::kC));
}

TEST_F(ExprParserTest, RustVariableDecl) {
  EXPECT_EQ(
      "LOCAL_VAR_DECL(i, 0)\n"
      " <Rust-style auto>\n"
      " BINARY_OP(+)\n"
      "  LITERAL(0)\n"
      "  LITERAL(76)\n",
      GetParseString("let i = 0 + 76;", ExprLanguage::kRust));

  EXPECT_EQ(
      "LOCAL_VAR_DECL(i, 0)\n"
      " i32\n"
      " ;\n",
      GetParseString("let i:i32;", ExprLanguage::kRust));

  // Note the type names in the output are formatted like C (i32*). This is just the default
  // formatting of the type name system whose names aren't really designed for final consumption.
  EXPECT_EQ(
      "LOCAL_VAR_DECL(i, 0)\n"
      " i32*\n"
      " IDENTIFIER(\"something\")\n",
      GetParseString("let i:&i32 = something", ExprLanguage::kRust));

  EXPECT_EQ(
      "LOCAL_VAR_DECL(i, 0)\n"
      " Type*\n"
      " ADDRESS_OF\n"
      "  IDENTIFIER(\"something\")\n",
      GetParseString("let i:&mut Type = &something", ExprLanguage::kRust));
}

TEST_F(ExprParserTest, LocalVarAccess) {
  EXPECT_EQ(
      "BLOCK\n"
      " BLOCK\n"
      "  LOCAL_VAR_DECL(i, 0)\n"
      "   int\n"
      "   LITERAL(54)\n"
      "  BINARY_OP(=)\n"
      "   LOCAL_VAR(0)\n"
      "   BINARY_OP(+)\n"
      "    LOCAL_VAR(0)\n"
      "    LITERAL(23)\n"
      " BLOCK\n"
      "  BINARY_OP(=)\n"
      "   IDENTIFIER(\"i\")\n"
      "   LITERAL(2)\n"
      "  LOCAL_VAR_DECL(j, 0)\n"
      "   <C++-style auto>\n"
      "   LITERAL(1)\n"
      "  LOCAL_VAR_DECL(k, 1)\n"
      "   double\n"
      "   LITERAL(0)\n",
      GetParseString(
          "{\n"
          "  {\n"
          "    int i = 54;\n"
          "    i = i + 23;\n"
          "  }\n"
          "  {\n"
          "    i = 2;\n"       // No local in scope w/ this name, should be an identifier.
          "    auto j = 1;\n"  // Gets assigned the same slot as i above since i went out of scope.
          "    double k = 0;\n"  // Gets assigned next slot.
          "  }"
          "}",
          ExprLanguage::kC));
}

}  // namespace zxdb

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/expr_parser.h"

#include <sstream>

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/string_util.h"
#include "src/developer/debug/zxdb/expr/expr_tokenizer.h"
#include "src/developer/debug/zxdb/symbols/collection.h"

namespace zxdb {

namespace {

// This name looker-upper declares anything beginning with "Namespace" is a
// namespace, anything beginning with "Template" is a template, and anything
// beginning with "Type" is a type.
FoundName TestLookupName(const ParsedIdentifier& ident, const FindNameOptions& opts) {
  const ParsedIdentifierComponent& comp = ident.components().back();
  const std::string& name = comp.name();

  if (opts.find_namespaces && StringBeginsWith(name, "Namespace"))
    return FoundName(FoundName::kNamespace, ident.GetFullName());
  if (opts.find_types && StringBeginsWith(name, "Type")) {
    // Make up a random class to go with the type.
    auto type = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
    type->set_assigned_name("Type");

    // NOTE: This doesn't qualify the type with namespaces or classes present
    // in the identifier so qualified names ("Namespace::Type") won't convert
    // to strings properly. This could be added if necessary.
    return FoundName(std::move(type));
  }
  if (opts.find_templates && StringBeginsWith(name, "Template")) {
    if (comp.has_template()) {
      // Assume templates with arguments are types.
      auto collection = fxl::MakeRefCounted<Collection>(DwarfTag::kClassType);
      // This name won't always be correct if the input has a namespace or other qualifier, but is
      // good enough for this test.
      collection->set_assigned_name(ident.GetFullName());
      return FoundName(std::move(collection));
    }
    return FoundName(FoundName::kTemplate, ident.GetFullName());
  }
  return FoundName();
}

}  // namespace

class ExprParserTest : public testing::Test {
 public:
  ExprParserTest() = default;

  // Valid after Parse() is called.
  ExprParser& parser() { return *parser_; }

  fxl::RefPtr<ExprNode> Parse(const char* input, NameLookupCallback name_lookup) {
    return Parse(input, ExprLanguage::kC, std::move(name_lookup));
  }

  fxl::RefPtr<ExprNode> Parse(const char* input, ExprLanguage lang = ExprLanguage::kC,
                              NameLookupCallback name_lookup = NameLookupCallback()) {
    parser_.reset();

    tokenizer_ = std::make_unique<ExprTokenizer>(input, lang);
    if (!tokenizer_->Tokenize()) {
      ADD_FAILURE() << "Tokenization failure: " << input;
      return nullptr;
    }

    parser_ = std::make_unique<ExprParser>(tokenizer_->TakeTokens(), tokenizer_->language(),
                                           std::move(name_lookup));
    return parser_->Parse();
  }

  // Does the parse and returns the string dump of the structure.
  std::string GetParseString(const char* input,
                             NameLookupCallback name_lookup = NameLookupCallback()) {
    return GetParseString(input, ExprLanguage::kC, std::move(name_lookup));
  }

  std::string GetParseString(const char* input, ExprLanguage lang, NameLookupCallback name_lookup) {
    auto root = Parse(input, lang, std::move(name_lookup));
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
};

TEST_F(ExprParserTest, Empty) {
  auto result = Parse("");
  ASSERT_FALSE(result);
  EXPECT_EQ("No input to parse.", parser().err().msg());
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

  EXPECT_EQ("Expected identifier for right-hand-side of \".\".", parser().err().msg());
  EXPECT_EQ(4u, parser().error_token().byte_offset());
  EXPECT_EQ(".", parser().error_token().value());
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

  // This error reports the "->" as the location, one could also imagine
  // reporting the right-side token (if any) instead.
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
  // These should be left-associative so do the "." first, then the "->".
  // When evaluating the tree, one first evaluates the left side of the
  // accessor, then the right, which is why it looks backwards in the dump.
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

  EXPECT_EQ("Unexpected input, did you forget an operator?", parser().err().msg());
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

  // "*" and "&" should be right-associative with respect to each other but
  // lower precedence than -> and [].
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
  EXPECT_EQ("Expected expression for '*'.", parser().err().msg());
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

// This test covers that the extent of number tokens are identified properly.
// Actually converting the strings to integers should be covered by the number
// parser tests.
TEST_F(ExprParserTest, IntegerLiterals) {
  EXPECT_EQ("LITERAL(5)\n", GetParseString("5"));
  EXPECT_EQ("LITERAL(5ull)\n", GetParseString(" 5ull"));
  EXPECT_EQ("LITERAL(0xAbC)\n", GetParseString("0xAbC"));
  EXPECT_EQ("LITERAL(0o551)\n", GetParseString("  0o551 "));
  EXPECT_EQ("LITERAL(0123)\n", GetParseString("0123"));
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
  EXPECT_EQ("IDENTIFIER(\"foo\")\n", GetParseString("foo"));
  EXPECT_EQ("IDENTIFIER(::\"foo\")\n", GetParseString("::foo"));
  EXPECT_EQ("IDENTIFIER(::\"foo\"; ::\"bar\")\n", GetParseString("::foo :: bar"));

  auto result = Parse("::");
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected name after '::'.", parser().err().msg());

  result = Parse(":: :: name");
  ASSERT_FALSE(result);
  EXPECT_EQ("Could not identify thing to the left of '::' as a type or namespace.",
            parser().err().msg());

  result = Parse("foo bar");
  ASSERT_FALSE(result);
  EXPECT_EQ("Unexpected identifier, did you forget an operator?", parser().err().msg());

  // It's valid to have identifiers with colons in them to access class members
  // (this is how you provide an explicit base class).
  /* TODO(brettw) convert an accessor to use an Identifier for the name so this
     test works.
  EXPECT_EQ(
      "ACCESSOR(->)\n"
      "  IDENTIFIER(\"foo\")\n",
      GetParseString("foo->Baz::bar"));
  */
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
      GetParseString("2<<2<static_cast<Template<int>>(2)>>2>2", &TestLookupName));
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
      GetParseString("ns::Foo<int>::GetCurrent()"));

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
  EXPECT_EQ("Expected ')' to match. Hit the end of input instead.", parser().err().msg());

  // Arguments not separated by commas.
  result = Parse("Call(a b)");
  ASSERT_FALSE(result);
  EXPECT_EQ("Unexpected identifier, did you forget an operator?", parser().err().msg());

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

TEST_F(ExprParserTest, Templates) {
  EXPECT_EQ(R"(IDENTIFIER("foo",<>))"
            "\n",
            GetParseString("foo<>"));
  EXPECT_EQ(R"(IDENTIFIER("foo",<"Foo">))"
            "\n",
            GetParseString("foo<Foo>"));
  EXPECT_EQ(R"(IDENTIFIER("foo",<"Foo", "5">))"
            "\n",
            GetParseString("foo< Foo,5 >"));
  EXPECT_EQ(
      R"(IDENTIFIER("std"; ::"map",<"Key", "Value", "std::less<Key>", "std::allocator<std::pair<Key const, Value>>">; ::"insert"))"
      "\n",
      GetParseString("std::map<Key, Value, std::less<Key>, "
                     "std::allocator<std::pair<Key const, Value>>>::insert"));

  // Unmatched "<" error. This is generated by the parser because it's
  // expecting the match for the outer level.
  auto result = Parse("std::map<Key, Value");
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected '>' to match. Hit the end of input instead.", parser().err().msg());

  // This unmatched token is generated by the template type skipper which is
  // why the error message is slightly different (both are OK).
  result = Parse("std::map<key[, value");
  ASSERT_FALSE(result);
  EXPECT_EQ("Unmatched '['.", parser().err().msg());

  // Duplicate template spec.
  result = Parse("Foo<Bar><Baz>");
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected expression after '>'.", parser().err().msg());

  // Empty value.
  result = Parse("Foo<1,,2>");
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected template parameter.", parser().err().msg());

  // Trailing comma.
  result = Parse("Foo<Bar,>");
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

TEST_F(ExprParserTest, Comparison) {
  EXPECT_EQ(
      "BINARY_OP(!=)\n"
      " IDENTIFIER(\"a\",<\"int\">; ::\"foo\")\n"
      " LITERAL(0)\n",
      GetParseString("a<int>::foo != 0"));

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
      "  LITERAL(2)\n"
      " BINARY_OP(>=)\n"
      "  IDENTIFIER(\"a\")\n"
      "  LITERAL(4)\n",
      GetParseString("1 <= 2 || a >= 4"));

  EXPECT_EQ(
      "BINARY_OP(<=>)\n"
      " IDENTIFIER(\"a\")\n"
      " LITERAL(4)\n",
      GetParseString("a <=> 4"));
}

// Tests parsing identifier names that require lookups from the symbol system.
// See TestLookupName above for the mocked symbol rules.
TEST_F(ExprParserTest, NamesWithSymbolLookup) {
  // Bare namespace is an error.
  auto result = Parse("Namespace", &TestLookupName);
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected expression after namespace name.", parser().err().msg());

  // Bare template is an error.
  result = Parse("Template", &TestLookupName);
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected template args after template name.", parser().err().msg());

  // Nothing after "::"
  result = Parse("Namespace::", &TestLookupName);
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected name after '::'.", parser().err().msg());

  // Can't put a template on a type that's not a template.
  result = Parse("Type<int>", &TestLookupName);
  ASSERT_FALSE(result);
  // This error message might change with future type support because it might
  // look like a comparison between a type and an int.
  EXPECT_EQ("Template parameters not valid on this object type.", parser().err().msg());

  // Can't put a template on a namespace.
  result = Parse("Namespace<int>", &TestLookupName);
  ASSERT_FALSE(result);
  EXPECT_EQ("Template parameters not valid on this object type.", parser().err().msg());

  // Good type name.
  EXPECT_EQ(
      "FUNCTIONCALL\n"
      " IDENTIFIER(\"Namespace\"; ::\"Template\",<\"int\">; ::\"fn\")\n",
      GetParseString("Namespace::Template<int>::fn()", &TestLookupName));
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
  EXPECT_EQ("TYPE(Type)\n", GetParseString("Type", &TestLookupName));
  EXPECT_EQ("IDENTIFIER(\"NotType\")\n", GetParseString("NotType", &TestLookupName));

  EXPECT_EQ("TYPE(const Type)\n", GetParseString("const Type", &TestLookupName));
  EXPECT_EQ("TYPE(const Type)\n", GetParseString("Type const", &TestLookupName));

  // It would be better it this printed as "const volatile Type" but our
  // heuristic for moving modifiers to the beginning isn't good enough.
  EXPECT_EQ("TYPE(volatile Type const)\n", GetParseString("const volatile Type", &TestLookupName));

  // Duplicate const qualifications.
  auto result = Parse("const Type const", &TestLookupName);
  ASSERT_FALSE(result);
  EXPECT_EQ("Duplicate 'const' type qualification.", parser().err().msg());
  result = Parse("const const Type", &TestLookupName);
  ASSERT_FALSE(result);
  EXPECT_EQ("Duplicate 'const' type qualification.", parser().err().msg());

  EXPECT_EQ("TYPE(Type*)\n", GetParseString("Type*", &TestLookupName));
  EXPECT_EQ("TYPE(Type**)\n", GetParseString("Type * *", &TestLookupName));
  EXPECT_EQ("TYPE(Type&&)\n", GetParseString("Type &&", &TestLookupName));
  EXPECT_EQ("TYPE(Type&**)\n", GetParseString("Type&**", &TestLookupName));
  EXPECT_EQ("TYPE(volatile Type* restrict* const)\n",
            GetParseString("Type volatile *restrict* const", &TestLookupName));

  // "const" should force us into type mode.
  result = Parse("const NonType", &TestLookupName);
  ASSERT_FALSE(result);
  EXPECT_EQ("Expected a type name but could not find a type named 'NonType'.",
            parser().err().msg());

  // Try sizeof() with both a type and a non-type.
  EXPECT_EQ(
      "SIZEOF\n"
      " TYPE(Type*)\n",
      GetParseString("sizeof(Type*)", &TestLookupName));
  EXPECT_EQ(
      "SIZEOF\n"
      " IDENTIFIER(\"foo\")\n",
      GetParseString("sizeof(foo)", &TestLookupName));
}

TEST_F(ExprParserTest, C_Cast) {
  EXPECT_EQ(
      "CAST(C)\n"
      " TYPE(Type)\n"
      " IDENTIFIER(\"a\")\n",
      GetParseString("(Type)(a)", &TestLookupName));

  EXPECT_EQ(
      "BINARY_OP(&&)\n"
      " CAST(C)\n"
      "  TYPE(Type*)\n"
      "  IDENTIFIER(\"a\")\n"
      " IDENTIFIER(\"b\")\n",
      GetParseString("(Type*)a && b", &TestLookupName));

  EXPECT_EQ(
      "CAST(C)\n"
      " TYPE(Type)\n"
      " ACCESSOR(->)\n"
      "  ARRAY_ACCESS\n"
      "   IDENTIFIER(\"a\")\n"
      "   LITERAL(0)\n"
      "  b\n",
      GetParseString("(Type)a[0]->b", &TestLookupName));

  // Looks like a cast but it's not a type.
  auto result = Parse("(NotType)a", &TestLookupName);
  EXPECT_FALSE(result);
  EXPECT_EQ("Unexpected input, did you forget an operator?", parser().err().msg());
}

TEST_F(ExprParserTest, RustCast) {
  EXPECT_EQ(
      "CAST(Rust)\n"
      " TYPE(Type)\n"
      " IDENTIFIER(\"a\")\n",
      GetParseString("a as Type", ExprLanguage::kRust, &TestLookupName));

  EXPECT_EQ(
      "BINARY_OP(&&)\n"
      " CAST(Rust)\n"
      "  TYPE(Type*)\n"
      "  IDENTIFIER(\"a\")\n"
      " IDENTIFIER(\"b\")\n",
      GetParseString("a as *Type && b", ExprLanguage::kRust, &TestLookupName));

  EXPECT_EQ(
      "BINARY_OP(&&)\n"
      " CAST(Rust)\n"
      "  TYPE(Type*******)\n"
      "  IDENTIFIER(\"a\")\n"
      " IDENTIFIER(\"b\")\n",
      GetParseString("a as &mut &mut && mut &*&Type && b", ExprLanguage::kRust, &TestLookupName));

  EXPECT_EQ(
      "CAST(Rust)\n"
      " TYPE(Type)\n"
      " ACCESSOR(->)\n"
      "  ARRAY_ACCESS\n"
      "   IDENTIFIER(\"a\")\n"
      "   LITERAL(0)\n"
      "  b\n",
      GetParseString("a[0]->b as Type", ExprLanguage::kRust, &TestLookupName));

  // We can't actually cast to tuple, so these wouldn't evaluate, but we want to test the type
  // parsing anyway.
  EXPECT_EQ(
      "CAST(Rust)\n"
      " TYPE((Type, Type, Type))\n"
      " IDENTIFIER(\"a\")\n",
      GetParseString("a as (Type, Type, Type)", ExprLanguage::kRust, &TestLookupName));

  EXPECT_EQ(
      "CAST(Rust)\n"
      " TYPE(())\n"
      " IDENTIFIER(\"a\")\n",
      GetParseString("a as ()", ExprLanguage::kRust, &TestLookupName));

  EXPECT_EQ(
      "CAST(Rust)\n"
      " TYPE((Type,))\n"
      " IDENTIFIER(\"a\")\n",
      GetParseString("a as (Type,)", ExprLanguage::kRust, &TestLookupName));

  EXPECT_EQ(
      "CAST(Rust)\n"
      " TYPE(Type)\n"
      " IDENTIFIER(\"a\")\n",
      GetParseString("a as (Type)", ExprLanguage::kRust, &TestLookupName));

  // Looks like a cast but it's not a type.
  auto result = Parse("a as NotType", ExprLanguage::kRust, &TestLookupName);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected a type name but could not find a type named 'NotType'.",
            parser().err().msg());

  // Rust cast but we're not in rust
  result = Parse("a as Type", ExprLanguage::kC, &TestLookupName);
  EXPECT_FALSE(result);
  EXPECT_EQ("Unexpected identifier, did you forget an operator?", parser().err().msg());
}

TEST_F(ExprParserTest, BadRustTuples) {
  auto result = Parse("a as (Type, NotType)", ExprLanguage::kRust, &TestLookupName);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected a type name but could not find a type named 'NotType'.",
            parser().err().msg());

  // Missing comma
  result = Parse("a as (Type Type)", ExprLanguage::kRust, &TestLookupName);
  EXPECT_FALSE(result);
  EXPECT_EQ("This looks like a declaration which is not supported.", parser().err().msg());

  // Missing end
  result = Parse("a as (Type, Type", ExprLanguage::kRust, &TestLookupName);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected ')' or ',' before end of input.", parser().err().msg());

  // Missing end with comma
  result = Parse("a as (Type,", ExprLanguage::kRust, &TestLookupName);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected ')' or ',' before end of input.", parser().err().msg());

  // Missing end, no groupings.
  result = Parse("a as (Type", ExprLanguage::kRust, &TestLookupName);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected ')' or ',' before end of input.", parser().err().msg());
}

TEST_F(ExprParserTest, CppCast) {
  EXPECT_EQ(
      "CAST(reinterpret_cast)\n"
      " TYPE(Type*)\n"
      " IDENTIFIER(\"a\")\n",
      GetParseString("reinterpret_cast<Type*>(a)", &TestLookupName));

  EXPECT_EQ(
      "CAST(static_cast)\n"
      " TYPE(Type*)\n"
      " IDENTIFIER(\"a\")\n",
      GetParseString("static_cast<Type*>(a)", &TestLookupName));

  EXPECT_EQ(
      "CAST(reinterpret_cast)\n"
      " TYPE(const Type&&)\n"
      " BINARY_OP(&&)\n"
      "  IDENTIFIER(\"x\")\n"
      "  IDENTIFIER(\"y\")\n",
      GetParseString("reinterpret_cast<  const Type&& >( x && y)", &TestLookupName));

  auto result = Parse("reinterpret_cast<", &TestLookupName);
  EXPECT_FALSE(result);
  EXPECT_EQ("Expected type name before end of input.", parser().err().msg());
}

TEST_F(ExprParserTest, ParseIdentifier) {
  Err err;

  // Empty input.
  ParsedIdentifier empty_ident;
  err = ExprParser::ParseIdentifier("", &empty_ident);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("No input to parse.", err.msg());
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

// "PLT" breakpoints are breakpoints set on ELF imports rather than DWARF
// symbols. We need to be able to parse them as an identifier even though it's
// not a valid C++ name. This can be changed in the future if we have a better
// way of identifying these.
TEST_F(ExprParserTest, PltName) {
  Identifier ident;
  Err err = ExprParser::ParseIdentifier("__stack_chk_fail@plt", &ident);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ("\"__stack_chk_fail@plt\"", ident.GetDebugName());
}

}  // namespace zxdb

// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/parser.h"

#include <lib/syslog/cpp/macros.h>

#include "src/developer/shell/parser/ast.h"
#include "src/developer/shell/parser/combinators.h"
#include "src/developer/shell/parser/error.h"
#include "src/developer/shell/parser/text_match.h"

namespace shell::parser {
namespace {

ParseResult Whitespace(ParseResult prefix);

// Create a parser that runs a sequence of parsers consecutively, with optional whitespace parsed
// between each parser.
fit::function<ParseResult(ParseResult)> WSSeq(fit::function<ParseResult(ParseResult)> first) {
  return Seq(Maybe(Whitespace), std::move(first), Maybe(Whitespace));
}

template <typename... Args>
fit::function<ParseResult(ParseResult)> WSSeq(fit::function<ParseResult(ParseResult)> first,
                                              Args... args) {
  return Seq(Maybe(Whitespace), std::move(first), WSSeq(std::move(args)...));
}

ParseResult IdentifierCharacter(ParseResult prefix);

// Parse a keyword.
template <typename T = ast::Terminal>
fit::function<ParseResult(ParseResult)> KW(const std::string& keyword) {
  return Seq(Token<T>(keyword), Not(IdentifierCharacter));
}

// Token Rules -------------------------------------------------------------------------------------

ParseResult IdentifierCharacter(ParseResult prefix) {
  return CharGroup("a-zA-Z0-9_")(std::move(prefix));
}

ParseResult Whitespace(ParseResult prefix) {
  return NT<ast::Whitespace>(
      OnePlus(Alt(AnyChar(" \n\r\t"), Seq(Token("#"), ZeroPlus(AnyCharBut("\n")), Token("\n")))))(
      std::move(prefix));
}

ParseResult Digit(ParseResult prefix) { return CharGroup("0-9")(std::move(prefix)); }

ParseResult HexDigit(ParseResult prefix) { return CharGroup("a-fA-F0-9")(std::move(prefix)); }

ParseResult UnescapedIdentifier(ParseResult prefix) {
  return Token<ast::UnescapedIdentifier>(OnePlus(IdentifierCharacter))(std::move(prefix));
}

ParseResult PathCharacter(ParseResult prefix) {
  return Seq(Not(Whitespace), AnyCharBut("`&;|/\\()[]{}"))(std::move(prefix));
}

ParseResult PathElement(ParseResult prefix) {
  return Alt(Token<ast::PathEscape>(Seq(Token("\\"), AnyChar)),
             Token<ast::PathElement>(OnePlus(PathCharacter)),
             Seq(Token<ast::PathEscape>("`"), Token<ast::PathElement>(ZeroPlus(AnyCharBut("`"))),
                 Token<ast::PathEscape>("`")))(std::move(prefix));
}

// Grammar Rules -----------------------------------------------------------------------------------

// Parses an identifier
//     myVariable
ParseResult Identifier(ParseResult prefix) {
  return NT<ast::Identifier>(Seq(Not(Digit), UnescapedIdentifier))(std::move(prefix));
}

// Parses a root path with at least one element and no trailing slash
//     /foo
//     /foo/bar
ParseResult RootPath(ParseResult prefix) {
  return OnePlus(Seq(Token<ast::PathSeparator>("/"), OnePlus(PathElement)))(std::move(prefix));
}

// Parses a path
//     /foo
//     /foo/bar
//     /foo/bar/
//     ./foo/bar/
//     ./
//     /
//     .
ParseResult Path(ParseResult prefix) {
  return NT<ast::Path>(Alt(Seq(Maybe(Token<ast::PathElement>(".")), RootPath, Maybe(Token("/"))),
                           Seq(Maybe(Token<ast::PathElement>(".")), Token<ast::PathSeparator>("/")),
                           Token<ast::PathElement>(".")))(std::move(prefix));
}

// Parses an unadorned decimal Integer
//     0
//     12345
//     12_345
ParseResult DecimalInteger(ParseResult prefix) {
  return Alt(
      Seq(Token<ast::DecimalGroup>("0"), Not(Digit)),
      Seq(Not(Token("0")), Token<ast::DecimalGroup>(OnePlus(Digit)),
          ZeroPlus(Seq(Token("_"), Token<ast::DecimalGroup>(OnePlus(Digit))))))(std::move(prefix));
}

// Parses a hexadecimal integer marked by '0x'
//     0x1234abcd
//     0x12_abcd
ParseResult HexInteger(ParseResult prefix) {
  return Seq(Token("0x"), Seq(Token<ast::HexGroup>(OnePlus(HexDigit)),
                              ZeroPlus(Seq(Token("_"), Token<ast::HexGroup>(OnePlus(HexDigit))))))(
      std::move(prefix));
}

// Parses an integer.
//     0
//     12345
//     12_345
//     0x1234abcd
//     0x12_abcd
ParseResult Integer(ParseResult prefix) {
  // TODO: Binary integers, once we ask the FIDL team about them.
  return NT<ast::Integer>(Alt(HexInteger, DecimalInteger))(std::move(prefix));
}

// Parse a Real (unimplemented).
ParseResult Real(ParseResult prefix) { return ParseResult::kEnd; }

// Parses an escape sequence.
//     \n
//     \r
//     \xF0
ParseResult EscapeSequence(ParseResult prefix) {
  return Alt(Token<ast::EscapeSequence>("\\n"), Token<ast::EscapeSequence>("\\t"),
             Token<ast::EscapeSequence>("\\\n"), Token<ast::EscapeSequence>("\\r"),
             Token<ast::EscapeSequence>("\\\\"), Token<ast::EscapeSequence>("\\\""),
             Token<ast::EscapeSequence>(
                 Seq(Token<ast::EscapeSequence>("\\u"), Multi(6, HexDigit))))(std::move(prefix));
}

// Parses a sequence of characters that might be within a string body.
//     The quick brown fox jumped over the lazy dog.
ParseResult StringEntity(ParseResult prefix) {
  return Alt(Token<ast::StringEntity>(OnePlus(AnyCharBut("\n\\\""))),
             EscapeSequence)(std::move(prefix));
}

// Parses an ordinary string literal.
//     "The quick brown fox jumped over the lazy dog."
//     "A newline.\nA tab\tA code point\xF0"
ParseResult NormalString(ParseResult prefix) {
  return NT<ast::String>(Seq(Token("\""), ZeroPlus(StringEntity), Token("\"")))(std::move(prefix));
}

// Parse an ordinary string literal, or a multiline string literal.
//     "The quick brown fox jumped over the lazy dog."
//     "A newline.\nA tab\tA code point\xF0"
// TODO: Decide on a MultiString syntax we like.
ParseResult String(ParseResult prefix) {
  // return Alt(NormalString, MultiString)(prefix);
  return NormalString(std::move(prefix));
}

// Parse an Atom (a simple literal value).
//     "The quick brown fox jumped over the lazy dog."
//     0x1234abcd
//     my_variable
//     3.2156
//     ./some/path
ParseResult Atom(ParseResult prefix) {
  return Alt(Identifier, String, Real, Integer, Path)(std::move(prefix));
}

ParseResult LogicalOr(ParseResult prefix);
const auto& SimpleExpression = LogicalOr;

// Parse a field in an object literal.
//     foo: 6
//     "bar & grill": "Open now"
ParseResult Field(ParseResult prefix) {
  return NT<ast::Field>(WSSeq(Alt(NormalString, Identifier), Token<ast::FieldSeparator>(":"),
                              SimpleExpression))(std::move(prefix));
}

// Parse the body of an object literal.
//     foo: 6
//     foo: 6, "bar & grill": "Open now",
ParseResult ObjectBody(ParseResult prefix) {
  return WSSeq(Field, ZeroPlus(WSSeq(Token(","), Field)), Maybe(Token(",")))(std::move(prefix));
}

// Parse an object literal.
//     {}
//     { foo: 6, "bar & grill": "Open now" }
//     { foo: { bar: 6 }, "bar & grill": "Open now" }
ParseResult Object(ParseResult prefix) {
  return NT<ast::Object>(WSSeq(Token("{"), Maybe(ObjectBody), Token("}")))(std::move(prefix));
}

// Parse a Value.
//     "The quick brown fox jumped over the lazy dog."
//     0x1234abcd
//     { foo: 3, bar: 6 }
ParseResult Value(ParseResult prefix) {
  /* Eventual full version of this rule is:
  return Alt(List, Object, Range, Lambda, Parenthetical, Block, If, Atom)(std::move(prefix));
  */
  return Alt(Object, Atom)(std::move(prefix));
}

// Unimplemented.
ParseResult Lookup(ParseResult prefix) { return Value(std::move(prefix)); }

// Unimplemented.
ParseResult Negate(ParseResult prefix) { return Lookup(std::move(prefix)); }

// Unimplemented.
ParseResult Mul(ParseResult prefix) { return Negate(std::move(prefix)); }

// Parse an addition expression.
//     2 + 2
ParseResult Add(ParseResult prefix) {
  return LAssoc<ast::AddSub>(Seq(Mul, Maybe(Whitespace)),
                             WSSeq(Token<ast::Operator>(AnyChar("+-")), Mul))(std::move(prefix));
}

// Unimplemented.
ParseResult Comparison(ParseResult prefix) { return Add(std::move(prefix)); }

// Unimplemented.
ParseResult LogicalNot(ParseResult prefix) { return Comparison(std::move(prefix)); }

// Unimplemented.
ParseResult LogicalAnd(ParseResult prefix) { return LogicalNot(std::move(prefix)); }

// Unimplemented.
ParseResult LogicalOr(ParseResult prefix) { return LogicalAnd(std::move(prefix)); }

// Parses an expression. This is effectively unimplemented right now.
ParseResult Expression(ParseResult prefix) {
  // Unimplemented
  return NT<ast::Expression>(SimpleExpression)(std::move(prefix));
}

// Parses a variable declaration:
//     var foo = 4.5
//     const foo = "Ham sandwich"
ParseResult VariableDecl(ParseResult prefix) {
  return NT<ast::VariableDecl>(WSSeq(Alt(KW<ast::Var>("var"), KW<ast::Const>("const")), Identifier,
                                     Token("="), Expression))(std::move(prefix));
}

// Parses the body of a program, but doesn't create an AST node. This is useful because the rule is
// recursive, but we want to flatten its structure.
ParseResult ProgramContent(ParseResult prefix) {
  /* Eventual full version of this rule is:
  return Alt(WSSeq(VariableDecl, Maybe(WSSeq(AnyChar(";&", "; or &"), ProgramMeta))),
             WSSeq(FunctionDecl, Program),
             WSSeq(Expression, Maybe(WSSeq(AnyChar(";&", "; or &"), ProgramMeta))),
  Empty)(prefix);
  */
  return Alt(WSSeq(VariableDecl, Maybe(WSSeq(AnyChar(";&"), ProgramContent))),
             Empty)(std::move(prefix));
}

}  // namespace

std::shared_ptr<ast::Node> Parse(std::string_view text) {
  auto res =
      NT<ast::Program>(Alt(Seq(ProgramContent, EOS), ErSkip("Unrecoverable parse error",
                                                            ZeroPlus(AnyChar))))(ParseResult(text));

  FX_DCHECK(res) << "Incorrectly handled parse error.";

  return res.node();
}

}  // namespace shell::parser

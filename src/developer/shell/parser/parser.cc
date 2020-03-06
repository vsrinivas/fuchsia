// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/shell/parser/parser.h"

#include "src/developer/shell/parser/ast.h"
#include "src/developer/shell/parser/combinators.h"
#include "src/developer/shell/parser/text_match.h"

namespace shell::parser {
namespace {

ParseResultStream Whitespace(ParseResultStream prefixes);

// Create a parser that runs a sequence of parsers consecutively, with optional whitespace parsed
// between each parser.
fit::function<ParseResultStream(ParseResultStream)> WSSeq(
    fit::function<ParseResultStream(ParseResultStream)> first) {
  return Seq(Maybe(Whitespace), std::move(first), Maybe(Whitespace));
}

template <typename... Args>
fit::function<ParseResultStream(ParseResultStream)> WSSeq(
    fit::function<ParseResultStream(ParseResultStream)> first, Args... args) {
  return Seq(Maybe(Whitespace), std::move(first), WSSeq(std::move(args)...));
}

ParseResultStream IdentifierCharacter(ParseResultStream prefixes);

// Parse a keyword.
template <typename T = ast::Terminal>
fit::function<ParseResultStream(ParseResultStream)> KW(const std::string& keyword) {
  return Seq(Token<T>(keyword), Not(IdentifierCharacter));
}

// Token Rules -------------------------------------------------------------------------------------

ParseResultStream IdentifierCharacter(ParseResultStream prefixes) {
  return CharGroup("identifier character", "a-zA-Z0-9_")(std::move(prefixes));
}

ParseResultStream Whitespace(ParseResultStream prefixes) {
  return NT<ast::Whitespace>(OnePlus(
      Alt(AnyChar("space", " \n\r\t"), Seq(Token("#"), ZeroPlus(AnyCharBut("non-newline", "\n")),
                                           Token("\n")))))(std::move(prefixes));
}

ParseResultStream Digit(ParseResultStream prefixes) {
  return CharGroup("digit", "0-9")(std::move(prefixes));
}

ParseResultStream HexDigit(ParseResultStream prefixes) {
  return CharGroup("hex digit", "a-fA-F0-9")(std::move(prefixes));
}

ParseResultStream UnescapedIdentifier(ParseResultStream prefixes) {
  return Token<ast::UnescapedIdentifier>(OnePlus(IdentifierCharacter))(std::move(prefixes));
}

// Grammar Rules -----------------------------------------------------------------------------------

// Parses an identifier
//     myVariable
ParseResultStream Identifier(ParseResultStream prefixes) {
  return NT<ast::Identifier>(Seq(Not(Digit), UnescapedIdentifier))(std::move(prefixes));
}

// Parses an unadorned decimal Integer
//     0
//     12345
//     12_345
ParseResultStream DecimalInteger(ParseResultStream prefixes) {
  return Alt(Seq(Token<ast::DecimalGroup>("0"), Not(Digit)),
             Seq(Not(Token("0")), Token<ast::DecimalGroup>(OnePlus(Digit)),
                 ZeroPlus(Seq(Token("_"), Token<ast::DecimalGroup>(OnePlus(Digit))))))(
      std::move(prefixes));
}

// Parses a hexadecimal integer marked by '0x'
//     0x1234abcd
//     0x12_abcd
ParseResultStream HexInteger(ParseResultStream prefixes) {
  return Seq(Token("0x"), Seq(Token<ast::HexGroup>(OnePlus(HexDigit)),
                              ZeroPlus(Seq(Token("_"), Token<ast::HexGroup>(OnePlus(HexDigit))))))(
      std::move(prefixes));
}

// Parses an integer.
//     0
//     12345
//     12_345
//     0x1234abcd
//     0x12_abcd
ParseResultStream Integer(ParseResultStream prefixes) {
  // TODO: Binary integers, once we ask the FIDL team about them.
  return NT<ast::Integer>(Alt(HexInteger, DecimalInteger))(std::move(prefixes));
}

// Parses an escape sequence.
//     \n
//     \r
//     \xF0
ParseResultStream EscapeSequence(ParseResultStream prefixes) {
  return Alt(Token<ast::EscapeSequence>("\\n"), Token<ast::EscapeSequence>("\\t"),
             Token<ast::EscapeSequence>("\\\n"), Token<ast::EscapeSequence>("\\r"),
             Token<ast::EscapeSequence>("\\\\"), Token<ast::EscapeSequence>("\\\""),
             Token<ast::EscapeSequence>(
                 Seq(Token<ast::EscapeSequence>("\\u"), Multi(6, HexDigit))))(std::move(prefixes));
}

// Parses a sequence of characters that might be within a string body.
//     The quick brown fox jumped over the lazy dog.
ParseResultStream StringEntity(ParseResultStream prefixes) {
  return Alt(Token<ast::StringEntity>(OnePlus(AnyCharBut("string body character", "\n\\\""))),
             EscapeSequence)(std::move(prefixes));
}

// Parses an ordinary string literal.
//     "The quick brown fox jumped over the lazy dog."
//     "A newline.\nA tab\tA code point\xF0"
ParseResultStream NormalString(ParseResultStream prefixes) {
  return NT<ast::String>(Seq(Token("\""), ZeroPlus(StringEntity), Token("\"")))(
      std::move(prefixes));
}

// Parse an ordinary string literal, or a multiline string literal.
//     "The quick brown fox jumped over the lazy dog."
//     "A newline.\nA tab\tA code point\xF0"
// TODO: Decide on a MultiString syntax we like.
ParseResultStream String(ParseResultStream prefixes) {
  // return Alt(NormalString, MultiString)(prefixes);
  return NormalString(std::move(prefixes));
}

// Parses an expression. This is effectively unimplemented right now.
ParseResultStream Expression(ParseResultStream prefixes) {
  // Unimplemented
  return NT<ast::Expression>(Alt(Integer, String))(std::move(prefixes));
}

// Parses a variable declaration:
//     var foo = 4.5
//     const foo = "Ham sandwich"
ParseResultStream VariableDecl(ParseResultStream prefixes) {
  return NT<ast::VariableDecl>(WSSeq(Alt(KW<ast::Var>("var"), KW<ast::Const>("const")), Identifier,
                                     Token("="), Expression))(std::move(prefixes));
}

// Parses the body of a program, but doesn't create an AST node. This is useful for parsing blocks
// where we might want to include the braces in the node's children.
ParseResultStream ProgramContent(ParseResultStream prefixes) {
  /* Eventual full version of this rule is:
  return Alt(WSSeq(VariableDecl, Maybe(WSSeq(AnyChar(";&", "; or &"), ProgramMeta))),
             WSSeq(FunctionDecl, Program),
             WSSeq(Expression, Maybe(WSSeq(AnyChar(";&", "; or &"), ProgramMeta))),
  Empty)(prefixes);
  */
  return Alt(WSSeq(VariableDecl, Maybe(WSSeq(AnyChar("; or &", ";&"), ProgramContent))),
             Empty)(std::move(prefixes));
}

}  // namespace

std::shared_ptr<ast::Node> Parse(std::string_view text) {
  return NT<ast::Program>(Seq(ProgramContent, EOS))(text).Next().node();
}

}  // namespace shell::parser

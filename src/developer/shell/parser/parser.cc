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
fit::function<ParseResultStream(ParseResultStream)> KW(const std::string& keyword) {
  return Seq(Token(keyword), Not(IdentifierCharacter));
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

ParseResultStream UnescapedIdentifier(ParseResultStream prefixes);
fit::function<ParseResultStream(ParseResultStream)> Thingy(int min) {
  if (min > 0) {
    return Seq(IdentifierCharacter, Thingy(min - 1));
  } else {
    return [](ParseResultStream prefixes) { return Maybe(Thingy(1))(std::move(prefixes)); };
  }
}

ParseResultStream UnescapedIdentifier(ParseResultStream prefixes) {
  return Token(OnePlus(IdentifierCharacter))(std::move(prefixes));
}

// Grammar Rules -----------------------------------------------------------------------------------

// Parses an identifier
//     myVariable
//     @0_day_variable
ParseResultStream Identifier(ParseResultStream prefixes) {
  return Alt(Seq(Token("@"), UnescapedIdentifier),
             Seq(Not(Digit), Not(Token("s#")), UnescapedIdentifier))(std::move(prefixes));
}

// Parses an expression. This is effectively unimplemented right now.
ParseResultStream Expression(ParseResultStream prefixes) {
  // Unimplemented
  return Digit(std::move(prefixes));
}

// Parses a variable declaration:
//     var foo = 4.5
ParseResultStream VariableDecl(ParseResultStream prefixes) {
  return NT<ast::VariableDecl>(WSSeq(KW("var"), Identifier, Token("="), Expression))(
      std::move(prefixes));
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

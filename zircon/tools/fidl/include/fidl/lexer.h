// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_LEXER_H_
#define ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_LEXER_H_

#include <assert.h>
#include <stdint.h>

#include <map>
#include <string_view>

#include "reporter.h"
#include "source_manager.h"
#include "token.h"

namespace fidl {

using reporter::Reporter;

// The lexer does not own the data it operates on. It merely takes a
// std::string_view and produces a stream of tokens and possibly a failure
// partway through.
// See https://fuchsia.dev/fuchsia-src/development/languages/fidl/reference/compiler#_lexing
// for additional context
class Lexer {
 public:
  // The Lexer assumes the final character is 0. This substantially
  // simplifies advancing to the next character.
  Lexer(const SourceFile& source_file, Reporter* reporter)
      : source_file_(source_file), reporter_(reporter) {
    keyword_table_ = {
#define KEYWORD(Name, Spelling) {Spelling, Token::Subkind::k##Name},
#include "fidl/token_definitions.inc"
#undef KEYWORD
    };
    current_ = data().data();
    end_of_file_ = current_ + data().size();
    previous_end_ = token_start_ = current_;
  }

  // Lexes and returns the next token. Must not be called again after returning
  // Token::Kind::kEndOfFile.
  Token Lex();

 private:
  std::string_view data() { return source_file_.data(); }

  constexpr char Peek() const;
  void Skip();
  char Consume();
  std::string_view Reset(Token::Kind kind);
  Token Finish(Token::Kind kind);

  void SkipWhitespace();

  Token LexEndOfStream();
  Token LexNumericLiteral();
  Token LexIdentifier();
  Token LexEscapedIdentifier();
  Token LexStringLiteral();
  Token LexCommentOrDocComment();

  const SourceFile& source_file_;
  std::map<std::string_view, Token::Subkind> keyword_table_;
  Reporter* reporter_;

  const char* current_ = nullptr;
  const char* end_of_file_ = nullptr;
  const char* token_start_ = nullptr;
  const char* previous_end_ = nullptr;
  size_t token_size_ = 0u;
};

}  // namespace fidl

#endif  // ZIRCON_TOOLS_FIDL_INCLUDE_FIDL_LEXER_H_

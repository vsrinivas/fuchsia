// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_LEXER_H_
#define ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_LEXER_H_

#include <assert.h>
#include <map>
#include <stdint.h>

#include "error_reporter.h"
#include "source_manager.h"
#include "string_view.h"
#include "token.h"

namespace fidl {

// The lexer does not own the data it operates on. It merely takes a
// StringView and produces a stream of tokens and possibly a failure
// partway through.
class Lexer {
public:
    // The Lexer assumes the final character is 0. This substantially
    // simplifies advancing to the next character.
    Lexer(const SourceFile& source_file, ErrorReporter* error_reporter)
        : source_file_(source_file), error_reporter_(error_reporter) {
        assert(data()[data().size() - 1] == 0);
        keyword_table_ = {
#define KEYWORD(Name, Spelling) {Spelling, Token::Subkind::k##Name},
#include "fidl/token_definitions.inc"
#undef KEYWORD
        };
        current_ = data().data();
        previous_end_ = token_start_ = current_;
    }

    Token Lex();
    Token LexNoComments();

private:
    StringView data() { return source_file_.data(); }

    constexpr char Peek() const;
    void Skip();
    char Consume();
    StringView Reset(Token::Kind kind);
    Token Finish(Token::Kind kind);

    void SkipWhitespace();

    Token LexEndOfStream();
    Token LexNumericLiteral();
    Token LexIdentifier();
    Token LexEscapedIdentifier();
    Token LexStringLiteral();
    Token LexCommentOrDocComment();

    const SourceFile& source_file_;
    std::map<StringView, Token::Subkind> keyword_table_;
    ErrorReporter* error_reporter_;

    const char* current_ = nullptr;
    const char* token_start_ = nullptr;
    const char* previous_end_ = nullptr;
    size_t token_size_ = 0u;
};

} // namespace fidl

#endif // ZIRCON_SYSTEM_HOST_FIDL_INCLUDE_FIDL_LEXER_H_

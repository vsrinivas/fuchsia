// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <stdint.h>

#include "identifier_table.h"
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
    Lexer(StringView data, IdentifierTable* identifier_table)
        : data_(data), identifier_table_(identifier_table) {
        assert(data[data.size() - 1] == 0);
        current_ = data_.data();
        token_start_ = current_;
    }
    Lexer(const Lexer&) = delete;

    Token Lex();
    Token LexNoComments();

private:
    constexpr char Peek() const;
    void Skip();
    char Consume();
    StringView Reset();
    Token Finish(Token::Kind kind);

    void SkipWhitespace();

    Token LexEndOfStream();
    Token LexNumericLiteral();
    Token LexIdentifier();
    Token LexEscapedIdentifier();
    Token LexStringLiteral();
    Token LexCXXComment();
    Token LexCComment();

    const StringView data_;
    const IdentifierTable* identifier_table_;

    const char* current_ = nullptr;
    const char* token_start_ = nullptr;
    uint32_t offset_ = 0u;
    size_t token_size_ = 0u;
};

} // namespace fidl

// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/lexer.h"

#include <ctype.h>

namespace fidl {

namespace {

bool IsIdentifierBody(char c) {
    return isalnum(c) || c == '_';
}

bool IsNumericLiteralBody(char c) {
    switch (c) {
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case 'a':
    case 'A':
    case 'b':
    case 'B':
    case 'c':
    case 'C':
    case 'd':
    case 'D':
    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'x':
    case 'X':
    case '-':
    case '_':
    case '.':
        return true;
    default:
        return false;
    }
}

} // namespace

constexpr char Lexer::Peek() const {
    return *current_;
}

void Lexer::Skip() {
    ++current_;
    ++token_start_;
}

char Lexer::Consume() {
    auto current = *current_;
    ++current_;
    ++token_size_;
    return current;
}

StringView Lexer::Reset() {
    auto data = StringView(token_start_, token_size_);
    token_start_ = current_;
    token_size_ = 0u;
    return data;
}

Token Lexer::Finish(Token::Kind kind) {
    return Token(SourceLocation(Reset(), source_file_), kind);
}

Token Lexer::LexEndOfStream() {
    return Finish(Token::Kind::EndOfFile);
}

Token Lexer::LexNumericLiteral() {
    while (IsNumericLiteralBody(Peek()))
        Consume();
    return Finish(Token::Kind::NumericLiteral);
}

Token Lexer::LexIdentifier() {
    while (IsIdentifierBody(Peek()))
        Consume();
    return identifier_table_->MakeIdentifier(Reset(), source_file_, /* escaped */ false);
}

Token Lexer::LexEscapedIdentifier() {
    // Reset() to drop the initial @ from the identifier.
    Reset();

    while (IsIdentifierBody(Peek()))
        Consume();
    return identifier_table_->MakeIdentifier(Reset(), source_file_, /* escaped */ true);
}

Token Lexer::LexStringLiteral() {
    auto last = Peek();

    // Lexing a "string literal" to the next matching delimiter.
    for (;;) {
        auto next = Consume();
        switch (next) {
        case 0:
            return Finish(Token::Kind::NotAToken);
        case '"':
            // This escaping logic is incorrect for the input: "\\"
            if (last != '\\')
                return Finish(Token::Kind::StringLiteral);
        // Fall through.
        default:
            last = next;
        }
    }
}

Token Lexer::LexComment() {
    // Consume the second /.
    assert(Peek() == '/');
    Consume();

    // Lexing a C++-style // comment. Go to the end of the line or
    // file.
    for (;;) {
        switch (Peek()) {
        case 0:
        case '\n':
            return Finish(Token::Kind::Comment);
        default:
            Consume();
            continue;
        }
    }
}

void Lexer::SkipWhitespace() {
    for (;;) {
        switch (Peek()) {
        case ' ':
        case '\n':
        case '\r':
        case '\t':
            Skip();
            continue;
        default:
            return;
        }
    }
}

Token Lexer::LexNoComments() {
    for (;;) {
        auto token = Lex();
        if (token.kind() == Token::Kind::Comment)
            continue;
        return token;
    }
}

Token Lexer::Lex() {
    SkipWhitespace();

    switch (Consume()) {
    case 0:
        return LexEndOfStream();

    case ' ':
    case '\n':
    case '\r':
    case '\t':
        assert(false && "Should have been handled by SkipWhitespace!");

    case '-':
        // Maybe the start of an arrow.
        if (Peek() == '>') {
            Consume();
            return Finish(Token::Kind::Arrow);
        }
    // Fallthrough
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        return LexNumericLiteral();

    case 'a':
    case 'A':
    case 'b':
    case 'B':
    case 'c':
    case 'C':
    case 'd':
    case 'D':
    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G':
    case 'h':
    case 'H':
    case 'i':
    case 'I':
    case 'j':
    case 'J':
    case 'k':
    case 'K':
    case 'l':
    case 'L':
    case 'm':
    case 'M':
    case 'n':
    case 'N':
    case 'o':
    case 'O':
    case 'p':
    case 'P':
    case 'q':
    case 'Q':
    case 'r':
    case 'R':
    case 's':
    case 'S':
    case 't':
    case 'T':
    case 'u':
    case 'U':
    case 'v':
    case 'V':
    case 'w':
    case 'W':
    case 'x':
    case 'X':
    case 'y':
    case 'Y':
    case 'z':
    case 'Z':
    case '_':
        return LexIdentifier();

    case '@':
        return LexEscapedIdentifier();

    case '"':
        return LexStringLiteral();

    case '/':
        // Maybe the start of a comment.
        switch (Peek()) {
        case '/':
            return LexComment();
        default:
            return Finish(Token::Kind::NotAToken);
        }

    case '(':
        return Finish(Token::Kind::LeftParen);
    case ')':
        return Finish(Token::Kind::RightParen);
    case '[':
        return Finish(Token::Kind::LeftSquare);
    case ']':
        return Finish(Token::Kind::RightSquare);
    case '{':
        return Finish(Token::Kind::LeftCurly);
    case '}':
        return Finish(Token::Kind::RightCurly);
    case '<':
        return Finish(Token::Kind::LeftAngle);
    case '>':
        return Finish(Token::Kind::RightAngle);

    case '.':
        return Finish(Token::Kind::Dot);
    case ',':
        return Finish(Token::Kind::Comma);
    case ';':
        return Finish(Token::Kind::Semicolon);
    case ':':
        return Finish(Token::Kind::Colon);
    case '?':
        return Finish(Token::Kind::Question);
    case '=':
        return Finish(Token::Kind::Equal);
    case '&':
        return Finish(Token::Kind::Ampersand);

    default:
        return Finish(Token::Kind::NotAToken);
    }
}

} // namespace fidl

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

StringView Lexer::Reset(Token::Kind kind) {
    auto data = StringView(token_start_, token_size_);
    if (kind != Token::Kind::kComment) {
        previous_end_ = token_start_ + token_size_;
    }
    token_start_ = current_;
    token_size_ = 0u;
    return data;
}

Token Lexer::Finish(Token::Kind kind) {
    StringView previous(previous_end_, token_start_ - previous_end_);
    StringView current(token_start_, token_size_);
    SourceLocation previous_location(previous, source_file_);
    return Token(previous_location,
                 SourceLocation(Reset(kind), source_file_), kind);
}

Token Lexer::LexEndOfStream() {
    return Finish(Token::Kind::kEndOfFile);
}

Token Lexer::LexNumericLiteral() {
    while (IsNumericLiteralBody(Peek()))
        Consume();
    return Finish(Token::Kind::kNumericLiteral);
}

Token Lexer::LexIdentifier() {
    while (IsIdentifierBody(Peek()))
        Consume();
    StringView previous(previous_end_, token_start_ - previous_end_);
    SourceLocation previous_end(previous, source_file_);
    return identifier_table_->MakeIdentifier(
        previous_end, Reset(Token::Kind::kNotAToken), source_file_, /* escaped */ false);
}

Token Lexer::LexEscapedIdentifier() {
    // Reset() to drop the initial @ from the identifier.
    Reset(Token::Kind::kComment);

    while (IsIdentifierBody(Peek()))
        Consume();
    StringView previous(previous_end_, token_start_ - previous_end_);
    SourceLocation previous_end(previous, source_file_);
    return identifier_table_->MakeIdentifier(
        previous_end, Reset(Token::Kind::kNotAToken), source_file_, /* escaped */ true);
}

Token Lexer::LexStringLiteral() {
    auto last = Peek();

    // Lexing a "string literal" to the next matching delimiter.
    for (;;) {
        auto next = Consume();
        switch (next) {
        case 0:
            return Finish(Token::Kind::kNotAToken);
        case '"':
            // This escaping logic is incorrect for the input: "\\"
            if (last != '\\')
                return Finish(Token::Kind::kStringLiteral);
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
            return Finish(Token::Kind::kComment);
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
        if (token.kind() == Token::Kind::kComment)
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
            return Finish(Token::Kind::kArrow);
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
            return Finish(Token::Kind::kNotAToken);
        }

    case '(':
        return Finish(Token::Kind::kLeftParen);
    case ')':
        return Finish(Token::Kind::kRightParen);
    case '[':
        return Finish(Token::Kind::kLeftSquare);
    case ']':
        return Finish(Token::Kind::kRightSquare);
    case '{':
        return Finish(Token::Kind::kLeftCurly);
    case '}':
        return Finish(Token::Kind::kRightCurly);
    case '<':
        return Finish(Token::Kind::kLeftAngle);
    case '>':
        return Finish(Token::Kind::kRightAngle);

    case '.':
        return Finish(Token::Kind::kDot);
    case ',':
        return Finish(Token::Kind::kComma);
    case ';':
        return Finish(Token::Kind::kSemicolon);
    case ':':
        return Finish(Token::Kind::kColon);
    case '?':
        return Finish(Token::Kind::kQuestion);
    case '=':
        return Finish(Token::Kind::kEqual);
    case '&':
        return Finish(Token::Kind::kAmpersand);

    default:
        return Finish(Token::Kind::kNotAToken);
    }
}

} // namespace fidl

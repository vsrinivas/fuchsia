// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fidl/lexer.h"

#include <assert.h>

#include <map>

namespace fidl {

using namespace diagnostics;

namespace {

bool IsIdentifierBody(char c) {
  switch (c) {
    case 'a':
    case 'b':
    case 'c':
    case 'd':
    case 'e':
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'j':
    case 'k':
    case 'l':
    case 'm':
    case 'n':
    case 'o':
    case 'p':
    case 'q':
    case 'r':
    case 's':
    case 't':
    case 'u':
    case 'v':
    case 'w':
    case 'x':
    case 'y':
    case 'z':
    case 'A':
    case 'B':
    case 'C':
    case 'D':
    case 'E':
    case 'F':
    case 'G':
    case 'H':
    case 'I':
    case 'J':
    case 'K':
    case 'L':
    case 'M':
    case 'N':
    case 'O':
    case 'P':
    case 'Q':
    case 'R':
    case 'S':
    case 'T':
    case 'U':
    case 'V':
    case 'W':
    case 'X':
    case 'Y':
    case 'Z':
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
    case '_':
      return true;
    default:
      return false;
  }
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

}  // namespace

constexpr char Lexer::Peek() const { return current_ < end_of_file_ ? *current_ : 0; }

void Lexer::Skip() {
  ++current_;
  ++token_start_;
}

char Lexer::Consume() {
  auto current = Peek();
  ++current_;
  ++token_size_;
  return current;
}

std::string_view Lexer::Reset(Token::Kind kind) {
  auto data = std::string_view(token_start_, token_size_);
  if (kind != Token::Kind::kComment) {
    previous_end_ = token_start_ + token_size_;
  }
  token_start_ = current_;
  token_size_ = 0u;
  return data;
}

Token Lexer::Finish(Token::Kind kind) {
  assert(kind != Token::Kind::kIdentifier);
  std::string_view previous(previous_end_, token_start_ - previous_end_);
  SourceSpan previous_span(previous, source_file_);
  return Token(previous_span, SourceSpan(Reset(kind), source_file_), kind, Token::Subkind::kNone);
}

Token Lexer::LexEndOfStream() { return Finish(Token::Kind::kEndOfFile); }

Token Lexer::LexNumericLiteral() {
  while (IsNumericLiteralBody(Peek()))
    Consume();
  return Finish(Token::Kind::kNumericLiteral);
}

Token Lexer::LexIdentifier() {
  while (IsIdentifierBody(Peek()))
    Consume();
  std::string_view previous(previous_end_, token_start_ - previous_end_);
  SourceSpan previous_end(previous, source_file_);
  std::string_view identifier_data = Reset(Token::Kind::kIdentifier);
  auto subkind = Token::Subkind::kNone;
  auto lookup = keyword_table_.find(identifier_data);
  if (lookup != keyword_table_.end())
    subkind = lookup->second;
  return Token(previous_end, SourceSpan(identifier_data, source_file_), Token::Kind::kIdentifier,
               subkind);
}

Token Lexer::LexStringLiteral() {
  auto last = Peek();

  // Lexing a "string literal" to the next matching delimiter.
  for (;;) {
    auto next = Consume();
    switch (next) {
      case 0:
        return LexEndOfStream();
      case '"':
        // This escaping logic is incorrect for the input: "\\"
        if (last != '\\')
          return Finish(Token::Kind::kStringLiteral);
        [[fallthrough]];
      default:
        last = next;
    }
  }
}

Token Lexer::LexCommentOrDocComment() {
  // Consume the second /.
  assert(Peek() == '/');
  Consume();

  // Check if it's a Doc Comment
  auto comment_type = Token::Kind::kComment;
  if (Peek() == '/') {
    comment_type = Token::Kind::kDocComment;
    Consume();
    // Anything with more than 3 slashes is a likely a section
    // break comment
    if (Peek() == '/') {
      comment_type = Token::Kind::kComment;
    }
  }

  // Lexing a C++-style // comment. Go to the end of the line or
  // file.
  for (;;) {
    switch (Peek()) {
      case 0:
      case '\n':
        return Finish(comment_type);
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

Token Lexer::Lex() {
  do {
    SkipWhitespace();

    switch (Consume()) {
      case 0:
        return LexEndOfStream();

      case ' ':
      case '\n':
      case '\r':
      case '\t':
        assert(false && "Should have been handled by SkipWhitespace!");
        break;

      case '-':
        // Maybe the start of an arrow.
        if (Peek() == '>') {
          Consume();
          return Finish(Token::Kind::kArrow);
        }
        [[fallthrough]];
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
        return LexIdentifier();

      case '"':
        return LexStringLiteral();

      case '/':
        // Maybe the start of a comment.
        switch (Peek()) {
          case '/':
            return LexCommentOrDocComment();
          default: {
            SourceSpan span(std::string_view(token_start_, token_size_), source_file_);
            reporter_->Report(ErrInvalidCharacter, span, span.data());
            continue;
          }
        }  // switch

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
      case '|':
        return Finish(Token::Kind::kPipe);

      default: {
        SourceSpan span(std::string_view(token_start_, token_size_), source_file_);
        reporter_->Report(ErrInvalidCharacter, span, span.data());
        continue;
      }
    }  // switch
  } while (true);
}

}  // namespace fidl

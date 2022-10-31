// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "tools/fidl/fidlc/include/fidl/lexer.h"

#include <assert.h>
#include <zircon/assert.h>

#include <map>

#include "tools/fidl/fidlc/include/fidl/diagnostics.h"

namespace fidl {

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

constexpr char Lexer::Peek() const {
  return current_ < end_of_file_ ? static_cast<char>(*current_) : 0;
}

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
  ZX_ASSERT(kind != Token::Kind::kIdentifier);
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
  auto lookup = token_subkinds.find(identifier_data);
  if (lookup != token_subkinds.end())
    subkind = lookup->second;
  return Token(previous_end, SourceSpan(identifier_data, source_file_), Token::Kind::kIdentifier,
               subkind);
}

static bool IsHexDigit(char c) {
  return ('0' <= c && c <= '9') || ('A' <= c && c <= 'F') || ('a' <= c && c <= 'f');
}

Token Lexer::LexStringLiteral() {
  enum State {
    kNormal,
    kEscaped,       // saw "\"
    kUnicode,       // saw "\u"
    kUnicodeBrace,  // saw "\u{"
  };
  auto state = kNormal;
  auto unicode_hex_digits = 0;

  // We've already consumed the opening '"'. Consume until the closing '"'.
  for (;;) {
    auto curr = Consume();
    // Check for EOF and invalid characters.
    switch (curr) {
      case 0:
        return LexEndOfStream();
      case '\n':
      case '\r': {
        SourceSpan span(std::string_view(current_ - 1, 1), source_file_);
        Fail(ErrUnexpectedLineBreak, span);
        state = kNormal;
        break;
      }
      default:
        if (curr >= 0 && curr <= 0x1f) {
          SourceSpan span(std::string_view(current_ - 1, 1), source_file_);
          char buf[3];
          snprintf(buf, sizeof buf, "%x", curr);
          Fail(ErrUnexpectedControlCharacter, span, std::string_view(buf));
          state = kNormal;
        }
        break;
    }
    // Main state machine.
    switch (state) {
      case kNormal:
        if (curr == '"')
          return Finish(Token::Kind::kStringLiteral);
        if (curr == '\\')
          state = kEscaped;
        break;
      case kEscaped:
        switch (curr) {
          case 'u':
            state = kUnicode;
            break;
          case '\\':
          case '"':
          case 'n':
          case 'r':
          case 't':
            state = kNormal;
            break;
          default:
            SourceSpan span(std::string_view(current_ - 2, 2), source_file_);
            Fail(ErrInvalidEscapeSequence, span, span.data());
            state = kNormal;
        }
        break;
      case kUnicode:
        if (curr == '{') {
          // Saw "\u{", now switch to lexing the hex digits.
          state = kUnicodeBrace;
          unicode_hex_digits = 0;
        } else {
          // Saw something like "\ua" which is invalid.
          SourceSpan span(std::string_view(current_ - 3, 2), source_file_);
          Fail(ErrUnicodeEscapeMissingBraces, span);
          if (curr == '"') {
            return Finish(Token::Kind::kStringLiteral);
          }
          state = kNormal;
        }
        break;
      case kUnicodeBrace:
        if (IsHexDigit(curr)) {
          // Saw a hex digit like "\u{a" or "\u{a3".
          ++unicode_hex_digits;
        } else if (curr == '"') {
          // The string literal ended before the closing "}".
          SourceSpan span(
              std::string_view(current_ - 4 - unicode_hex_digits, unicode_hex_digits + 3),
              source_file_);
          Fail(ErrUnicodeEscapeUnterminated, span);
          return Finish(Token::Kind::kStringLiteral);
        } else if (curr == '}') {
          // Saw "\u{...}", now validate the "..." part.
          if (unicode_hex_digits == 0) {
            SourceSpan span(
                std::string_view(current_ - 4 - unicode_hex_digits, unicode_hex_digits + 4),
                source_file_);
            Fail(ErrUnicodeEscapeEmpty, span);
          } else if (unicode_hex_digits > 6) {
            SourceSpan span(std::string_view(current_ - 1 - unicode_hex_digits, unicode_hex_digits),
                            source_file_);
            Fail(ErrUnicodeEscapeTooLong, span);
          } else {
            SourceSpan span(std::string_view(current_ - 1 - unicode_hex_digits, unicode_hex_digits),
                            source_file_);
            auto codepoint = utils::decode_unicode_hex(span.data());
            if (codepoint > 0x10ffff) {
              Fail(ErrUnicodeEscapeTooLarge, span, span.data());
            }
          }
          state = kNormal;
        } else {
          SourceSpan span(std::string_view(current_ - 1, 1), source_file_);
          Fail(ErrInvalidHexDigit, span, curr);
          state = kNormal;
        }
        break;
    }
  }
}

Token Lexer::LexCommentOrDocComment() {
  // Consume the second /.
  ZX_ASSERT(Peek() == '/');
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
  ZX_ASSERT_MSG(token_start_ <= end_of_file_, "already reached EOF");
  ZX_ASSERT_MSG(current_ <= end_of_file_ + 1, "current_ is past null terminator");

  do {
    SkipWhitespace();

    switch (Consume()) {
      case 0:
        return LexEndOfStream();

      case ' ':
      case '\n':
      case '\r':
      case '\t':
        ZX_PANIC("should have been handled by SkipWhitespace");
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
            Fail(ErrInvalidCharacter, span, span.data());
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

      case '@':
        return Finish(Token::Kind::kAt);
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
        Fail(ErrInvalidCharacter, span, span.data());
        continue;
      }
    }  // switch
  } while (true);
}

}  // namespace fidl

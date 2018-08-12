// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/expr_tokenizer.h"

#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_printf.h"

namespace zxdb {

namespace {

bool IsNameFirstChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

bool IsNameContinuingChar(char c) {
  return IsNameFirstChar(c) || (c >= '0' && c <= '9');
}

bool IsIntegerFirstChar(char c) { return c >= '0' && c <= '9'; }

bool IsIntegerContinuingChar(char c) {
  // The 'a'-'f' and 'x' allows hexadecimal numbers. The number will be
  // validated and interpreted later.
  return IsIntegerFirstChar(c) || c == 'x' || (c >= 'A' && c <= 'F') ||
         (c >= 'a' && c <= 'f');
}

}  // namespace

ExprTokenizer::ExprTokenizer(const std::string& input) : input_(input) {}

bool ExprTokenizer::Tokenize() {
  while (!done()) {
    AdvanceToNextToken();
    if (done())
      break;

    ExprToken::Type type = ClassifyCurrent();
    if (has_error())
      break;

    size_t token_begin = cur_;
    AdvanceToEndOfToken(type);
    if (has_error())
      break;

    size_t token_end = cur_;
    std::string token_value(&input_[token_begin], token_end - token_begin);
    tokens_.emplace_back(type, token_value, token_begin);
  }
  return !has_error();
}

// static
std::string ExprTokenizer::GetErrorContext(const std::string& input,
                                           size_t byte_offset) {
  // Index should be in range of the input string. Also allow indicating one
  // character past the end.
  FXL_DCHECK(byte_offset <= input.size());

  // Future enhancements:
  // - If we allow multiline expressions in the, the returned context should
  //   not cross newlines or it will be messed up.
  // - Input longer than 80 chars should be clipped to guarantee it doesn't
  //   wrap.

  std::string output;
  output = "  " + input + "\n  ";
  output.append(byte_offset, ' ');
  output.push_back('^');
  return output;
}

void ExprTokenizer::AdvanceOneChar() { cur_++; }

void ExprTokenizer::AdvanceToNextToken() {
  while (!at_end() && IsCurrentWhitespace())
    AdvanceOneChar();
}

void ExprTokenizer::AdvanceToEndOfToken(ExprToken::Type type) {
  switch (type) {
    case ExprToken::kInteger:
      do {
        AdvanceOneChar();
      } while (!at_end() && IsIntegerContinuingChar(cur_char()));
      break;

    case ExprToken::kName:
      do {
        AdvanceOneChar();
      } while (!at_end() && IsNameContinuingChar(cur_char()));
      break;

    case ExprToken::kArrow:
      // The classification code should already have validated there were two
      // characters available.
      AdvanceOneChar();
      AdvanceOneChar();
      break;

    case ExprToken::kDot:
    case ExprToken::kStar:
    case ExprToken::kAmpersand:
    case ExprToken::kLeftSquare:
    case ExprToken::kRightSquare:
    case ExprToken::kLeftParen:
    case ExprToken::kRightParen:
    case ExprToken::kMinus:
      AdvanceOneChar();  // All are one char.
      break;

    case ExprToken::kInvalid:
    case ExprToken::kNumTypes:
      FXL_NOTREACHED();
      err_ = Err("Internal parser error.");
      error_location_ = cur_;
      break;
  }
}

bool ExprTokenizer::IsCurrentWhitespace() const {
  FXL_DCHECK(!at_end());
  char c = input_[cur_];
  return c == 0x0A || c == 0x0D || c == 0x20;
}

ExprToken::Type ExprTokenizer::ClassifyCurrent() {
  FXL_DCHECK(!at_end());
  char cur = cur_char();

  if (cur >= '0' && cur <= '9')
    return ExprToken::kInteger;
  if (IsNameFirstChar(cur))
    return ExprToken::kName;

  switch (cur) {
    case '-':
      // Hyphen could be itself or an arrow, look ahead.
      if (cur_ < input_.size() - 1) {
        char following_char = input_[cur_ + 1];
        if (following_char == '>')
          return ExprToken::kArrow;
      }
      // Anything else is a standalone hyphen.
      return ExprToken::kMinus;
    case '.':
      return ExprToken::kDot;
    case '*':
      return ExprToken::kStar;
    case '&':
      return ExprToken::kAmpersand;
    case '[':
      return ExprToken::kLeftSquare;
    case ']':
      return ExprToken::kRightSquare;
    case '(':
      return ExprToken::kLeftParen;
    case ')':
      return ExprToken::kRightParen;
    default:
      error_location_ = cur_;
      err_ = Err(
          fxl::StringPrintf("Invalid character '%c' in expression.\n", cur) +
          GetErrorContext(input_, cur_));
      return ExprToken::kInvalid;
  }
}

}  // namespace zxdb

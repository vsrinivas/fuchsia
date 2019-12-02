// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/parse_string.h"

#include <ctype.h>
#include <stdlib.h>

#include "src/lib/fxl/logging.h"

namespace zxdb {

namespace {

// A character sequence made of any source character except for parentheses, backslash and spaces.
bool IsValidCRawStringDelimeter(char c) { return c != '(' && c != ')' && c != '\\' && !isspace(c); }

std::optional<StringLiteralBegin> DoesBeginRawCStringLiteral(std::string_view input, size_t begin) {
  // This only supports raw string literals and not the various flavors of Unicode prefixes.
  if (input.size() <= begin + 2 || input[begin] != 'R' || input[begin + 1] != '"')
    return std::nullopt;

  // Skip over the delimiter.
  size_t cur = begin + 2;
  while (input.size() > cur && IsValidCRawStringDelimeter(input[cur]))
    cur++;

  // Expecting a paren to begin the string.
  if (cur == input.size() || input[cur] != '(')
    return std::nullopt;

  StringLiteralBegin info;
  info.language = ExprLanguage::kC;
  info.is_raw = true;
  info.raw_marker = input.substr(begin + 2, cur - begin - 2);
  info.string_begin = begin;
  info.contents_begin = cur + 1;

  return info;
}

// Rust raw strings start with 'r', some number of '#' characters, and a quote.
std::optional<StringLiteralBegin> DoesBeginRawRustStringLiteral(std::string_view input,
                                                                size_t begin) {
  // This only supports "raw" strings, not "byte" strings. It could be enhanced in the future.
  if (input.size() <= begin + 2 || input[begin] != 'r' || input[begin + 1] != '#')
    return std::nullopt;

  size_t cur = begin + 1;
  while (input.size() > cur && input[cur] == '#')
    cur++;

  if (cur == input.size() || input[cur] != '"')
    return std::nullopt;

  StringLiteralBegin info;
  info.language = ExprLanguage::kRust;
  info.is_raw = true;
  info.raw_marker = input.substr(begin + 1, cur - begin - 1);
  info.string_begin = begin;
  info.contents_begin = cur + 1;

  return info;
}

// Determines if the current index marks the beginning of the end of the string. If it does,
// returns the index of the character immediately following the string (which might point to
// one-past-the-end of the input). Otherwise returns 0.
size_t EndsStringLiteral(std::string_view input, const StringLiteralBegin& info, size_t cur) {
  FXL_DCHECK(cur < input.size());

  if (!info.is_raw) {
    if (input[cur] == '"')
      return cur + 1;
    return 0;
  }

  switch (info.language) {
    case ExprLanguage::kC:
      if (input.size() - cur >= info.raw_marker.size() + 2) {
        if (input[cur] == ')' && input[cur + info.raw_marker.size() + 1] == '"' &&
            input.substr(cur + 1, info.raw_marker.size()) == info.raw_marker)
          return cur + info.raw_marker.size() + 2;
      }
      break;
    case ExprLanguage::kRust:
      if (input.size() - cur >= info.raw_marker.size() + 1) {
        if (input[cur] == '"' && input.substr(cur + 1, info.raw_marker.size()) == info.raw_marker)
          return cur + info.raw_marker.size() + 1;
      }
  }

  return 0;
}

bool IsHexDigit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}
bool IsOctalDigit(char c) { return c >= '0' && c <= '7'; }

// See HandleEscaped() below for the parameter description. |*cur| should point to the first hex
// digit.
Err HandleHexEscaped(std::string_view input, const StringLiteralBegin& info, size_t* cur,
                     size_t* error_location, std::string* result) {
  if (!IsHexDigit(input[*cur])) {
    *error_location = *cur;
    return Err("Expecting hex escape sequence.");
  }

  std::string hex_digits;
  switch (info.language) {
    case ExprLanguage::kC:
      // C reads hex digits until there are no more.
      for (size_t i = *cur; i < input.size() && IsHexDigit(input[i]); i++)
        hex_digits.push_back(input[i]);
      break;
    case ExprLanguage::kRust:
      // Rust requires exactly two characters.
      if (*cur + 1 >= input.size() || !IsHexDigit(input[*cur + 1])) {
        *error_location = *cur;
        return Err("Expecting two hex digits.");
      }
      hex_digits.push_back(input[*cur]);
      hex_digits.push_back(input[*cur + 1]);
      break;
  }

  char* endptr = nullptr;
  unsigned long value = strtoul(hex_digits.c_str(), &endptr, 16);
  if (endptr != hex_digits.data() + hex_digits.size()) {
    *error_location = *cur;
    return Err("Unexpected hex input.");
  }

  (*cur) += hex_digits.size();
  result->push_back(static_cast<unsigned char>(value));
  return Err();
}

// See HandleEscaped() below for the parameter description. |*cur| should point to the first octal
// digit.
Err HandleOctalEscaped(std::string_view input, const StringLiteralBegin& info, size_t* cur,
                       size_t* error_location, std::string* result) {
  if (!IsOctalDigit(input[*cur])) {
    *error_location = *cur;
    return Err("Expecting hex escape sequence.");
  }

  std::string octal_digits;
  for (size_t i = *cur; i < input.size() && IsOctalDigit(input[i]); i++)
    octal_digits.push_back(input[i]);

  char* endptr = nullptr;
  unsigned long value = strtoul(octal_digits.c_str(), &endptr, 8);
  if (endptr != octal_digits.data() + octal_digits.size()) {
    *error_location = *cur;
    return Err("Unexpected octal input.");
  }

  (*cur) += octal_digits.size();
  result->push_back(static_cast<unsigned char>(value));
  return Err();
}

// On input, |*cur| should point to a valid character in |input| immediately following a backslash.
// On success, |*cur| will be updated to point to the character immediately following the escape.
Err HandleEscaped(std::string_view input, const StringLiteralBegin& info, size_t* cur,
                  size_t* error_location, std::string* result) {
  // Shared C/Rust escape sequences.
  switch (input[*cur]) {
    // clang-format off
    case 'n':  result->push_back('\n'); ++(*cur); return Err();
    case 'r':  result->push_back('\r'); ++(*cur); return Err();
    case 't':  result->push_back('\t'); ++(*cur); return Err();
    case '\\': result->push_back('\\'); ++(*cur); return Err();
    case '\'': result->push_back('\''); ++(*cur); return Err();
    case '"':  result->push_back('"');  ++(*cur); return Err();
    default: break;
      // clang-format on
  }

  if (input[*cur] == 'x') {
    // Hex digit.
    ++(*cur);
    if (*cur == input.size()) {
      *error_location = *cur - 2;  // Point to backslash.
      return Err("End of input found in hex escape.");
    }
    return HandleHexEscaped(input, info, cur, error_location, result);
  }

  if (info.language == ExprLanguage::kC) {
    // C-specific escape sequences.
    switch (input[*cur]) {
      // clang-format off
      case '?':  result->push_back('?'); ++(*cur); return Err();
      case 'a':  result->push_back('\a'); ++(*cur); return Err();
      case 'b':  result->push_back('\b'); ++(*cur); return Err();
      case 'f':  result->push_back('\f'); ++(*cur); return Err();
      case 'v':  result->push_back('\v'); ++(*cur); return Err();
      default: break;
        // clang-format on
    }

    if (input[*cur] == 'u' || input[*cur] == 'U')
      return Err("Unicode escape sequences are not supported.");

    if (IsOctalDigit(input[*cur])) {
      // Octal.
      return HandleOctalEscaped(input, info, cur, error_location, result);
    }
  }

  if (info.language == ExprLanguage::kRust) {
    // Rust-specific escape sequences.
    if (input[*cur] == '0') {
      // Null.
      result->push_back(0);
      ++(*cur);
      return Err();
    }

    if (input[*cur] == 'u')
      return Err("Unicode escape sequences are not supported.");
  }

  *error_location = *cur - 1;  // Point to backslash.
  return Err("Unknown escape sequence.");
}

}  // namespace

std::optional<StringLiteralBegin> DoesBeginStringLiteral(ExprLanguage lang, std::string_view input,
                                                         size_t cur) {
  if (cur >= input.size())
    return std::nullopt;  // No room.

  StringLiteralBegin info;
  info.language = lang;

  if (input[cur] == '"') {
    // Regular literal string. Leave the raw string marker empty.
    info.string_begin = cur;
    info.contents_begin = cur + 1;
    return info;
  }

  switch (lang) {
    case ExprLanguage::kC:
      return DoesBeginRawCStringLiteral(input, cur);
    case ExprLanguage::kRust:
      return DoesBeginRawRustStringLiteral(input, cur);
  }

  FXL_NOTREACHED();
  return std::nullopt;
}

ErrOr<std::string> ParseStringLiteral(std::string_view input, const StringLiteralBegin& info,
                                      size_t* in_out_cur, size_t* error_location) {
  FXL_DCHECK(info.contents_begin <= input.size());

  std::string result;
  size_t cur = info.contents_begin;

  while (cur < input.size()) {
    if (size_t end = EndsStringLiteral(input, info, cur)) {
      *in_out_cur = end;
      return result;
    }

    if (!info.is_raw && input[cur] == '\\') {
      cur++;  // Advance over backslash.
      if (cur == input.size()) {
        *error_location = cur - 1;
        return Err("Hit end of input before the end of the escape sequence.");
      }

      Err err = HandleEscaped(input, info, &cur, error_location, &result);
      if (err.has_error())
        return err;
    } else {
      // Non-escaped.
      result.push_back(input[cur]);
      cur++;
    }
  }

  // Hit the end without an end-of-string.
  *error_location = info.string_begin;
  return Err("Hit end of input before the end of the string.");
}

}  // namespace zxdb

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_DEBUG_ZXDB_EXPR_NUMBER_PARSER_H_
#define SRC_DEVELOPER_DEBUG_ZXDB_EXPR_NUMBER_PARSER_H_

#include <string_view>

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/expr_language.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"

namespace zxdb {

class ExprToken;

// Converts the given string to a number. Currently this only handles integers (no floating point).
//
// It tries to compute a value of the correct type given the input, taking into account size
// suffixes and the magnitude of the number. The rules are somewhat simplified from C++ in that the
// base of the number is not considered and it will pick the smallest type that will fit (C++ has
// different rules for decimal numbers, see the .cc file).
ErrOrValue StringToNumber(std::string_view str);

struct IntegerPrefix {
  enum Sign { kPositive, kNegative };
  enum Base : int { kBin = 2, kOct = 8, kDec = 10, kHex = 16 };

  // Differentiates the two ways an octal prefix can be expressed.
  enum class OctalType {
    kC,    // "0123"
    kRust  // "0o123"
  };

  Sign sign = kPositive;
  Base base = kDec;

  // Valid when base == kOct;
  OctalType octal_type = OctalType::kC;
};

// Checks for a sign and base prefix for a number in the given string view. It does not check for
// overall number validity.
//
// The number prefix will be trimmed from the given string view so it contains only the part of the
// number after the prefix (if any). The base of the number will be returned.
//
// It is assumed whitespace has already been trimmed.
//
// If there is no prefix (including if it's not a valid number), it will report positive base 10 and
// not trim anything.
IntegerPrefix ExtractIntegerPrefix(std::string_view* s);

struct IntegerSuffix {
  enum Signed { kSigned, kUnsigned };
  bool type_signed = kSigned;

  // The numeric values allow these to be compared via integer comparisons.
  enum class Length { kInteger = 0, kLong = 1, kLongLong = 2 };
  Length length = Length::kInteger;
};

// Checks for a type suffix on a number in the given string view and returns the suffix structure.
// It does not check for overall number validity.
//
// On success, the number suffix ("u", "l", "ll") will be trimmed from the given string view so it
// contains only the part of the number before the suffix (if any).
//
// It is assumed whitespace has already been trimmed.
//
// If there is no suffix, it will return a signed integer and not trim anything. If the suffix is
// invalid, return the error.
ErrOr<IntegerSuffix> ExtractIntegerSuffix(std::string_view* s);

// Checks if the current input begins with a floating-point literal and returns its length if it
// does. Returns 0 if it does not begin a floating point literal.
//
// Whitespace is not stripped so leading whitespace will not be considered a floating point token.
// As in C, a leading '-' is not considered part of the token. "-23.5" is a unary '-' operator
// applied to a positive floating-point literal.
//
// On success, the identified token may not represent a valid floating-point number. It may have
// extra garbage in it, or may be malformed in various ways. It is just the range of text to be
// considered a float. See ValueForFloatToken for conversion + validation.
size_t GetFloatTokenLength(ExprLanguage lang, std::string_view input);

enum class FloatSuffix {
  kNone,   // No known suffix.
  kFloat,  // 'f' or "F" meaning "float" instead of a double.
  kLong,   // "l" or "L" meaning "long double".
};

// Identifies and strips the suffix from the end of a float token. The suffix is assumed to be the
// last character of the input.
FloatSuffix StripFloatSuffix(std::string_view* view);

// Given a floating-point token, returns the ExprValue for it if possible.
ErrOrValue ValueForFloatToken(ExprLanguage lang, const ExprToken& token);

}  // namespace zxdb

#endif  // SRC_DEVELOPER_DEBUG_ZXDB_EXPR_NUMBER_PARSER_H_

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <string_view>

#include "src/developer/debug/zxdb/common/err.h"

namespace zxdb {

class ExprValue;

// Converts the given string to a number. Currently this only handles integers
// (no floating point).
//
// It tries to compute a value of the correct type given the input, taking into
// acount size suffixes and the magnitude of the number. The rules are somewhat
// simplified from C++ in that the base of the number is not considered and it
// will pick the smallest type that will fit (C++ has different rules for
// decimal numbers, see the .cc file).
[[nodiscard]] Err StringToNumber(std::string_view str, ExprValue* output);

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

// Checks for a sign and base prefix for a number in the given string view. It
// does not check for overall number validity.
//
// The number prefix will be trimmed from the given string view so it contains
// only the part of the number after the prefix (if any). The base of the
// number will be returned.
//
// It is assumed whitespace has already been trimmed.
//
// If there is no prefix (including if it's not a valid number), it will report
// positive base 10 and not trim anything.
IntegerPrefix ExtractIntegerPrefix(std::string_view* s);

struct IntegerSuffix {
  enum Signed { kSigned, kUnsigned };
  bool type_signed = kSigned;

  // The numeric values allow these to be compared via integer comparisons.
  enum class Length { kInteger = 0, kLong = 1, kLongLong = 2 };
  Length length = Length::kInteger;
};

// Checks for a type suffix on a number in the given string view and fills the
// given structure. It does not check for overall number validity.
//
// On success, tne number suffix ("u", "l", "ll") will be trimmed from the
// given string view so it contains only the part of the number before the
// suffix (if any).
//
// It is assumed whitespace has already been trimmed.
//
// If there is no suffix, it will return a signed integer and not trim
// anything. If the suffix is invalid, return the error.
[[nodiscard]] Err ExtractIntegerSuffix(std::string_view* s,
                                       IntegerSuffix* suffix);

}  // namespace zxdb

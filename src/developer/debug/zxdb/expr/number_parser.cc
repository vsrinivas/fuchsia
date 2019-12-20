// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/number_parser.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>

#include "src/developer/debug/zxdb/expr/builtin_types.h"
#include "src/developer/debug/zxdb/expr/expr_token.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "third_party/double-conversion/double-conversion/double-conversion.h"

namespace zxdb {

namespace {

// Max values converted to a uint64_t.
constexpr uint64_t kSigned32Max = std::numeric_limits<int32_t>::max();
constexpr uint64_t kSigned64Max = std::numeric_limits<int64_t>::max();
constexpr uint64_t kUnsigned32Max = std::numeric_limits<uint32_t>::max();
constexpr uint64_t kUnsigned64Max = std::numeric_limits<uint64_t>::max();

// Absolute value of the smallest number that can be put in a signed 32-bit number. Be careful, the
// negative numbers hold one larger than the corresponding positive number which makes it hard to
// compute.
constexpr uint64_t kSigned32MaxAbsNeg = 0x80000000;
constexpr uint64_t kSigned64MaxAbsNeg = 0x8000000000000000ul;

// This hardcodes our current 64-bit type scheme where "long" and "long long" are both 64 bits, and
// "int" is 32. Note that we still support "long long" because it's surprising if you type "0x100ll"
// and don't get something called "long long" back.
//
// C++ has more rules about whether the input has a specific base (hex numbers prefer to be unsigned
// if possible), and the "l" suffix is particularly weird because it allows matching "unsigned long"
// while no other decimal numbers will match unsigned types without "u". Our requirements don't need
// all of these rules so keep things a bit simpler.
//
// See: https://en.cppreference.com/w/cpp/language/integer_literal
struct TypeLookup {
  const char* name;
  size_t byte_size;
  bool type_signed;

  // The largest positive value held by this type.
  uint64_t max_positive;

  // Absolute value of the most negative value held by this type. In the case of unsigned types,
  // this should hold the same value as the corresponding signed type. This allows "-23u" to specify
  // an unsigned version of the type that would normally hold "-23".
  uint64_t max_abs_negative;

  // Maximum suffix this type matches. If the number specifies "l" it will allow "long" or "long
  // long" but not int. Any lengths less than this will not match.
  IntegerSuffix::Length max_suffix;
} kTypeLookup[] = {
    // clang-format off
    // Name            bytes, signed max_positive    max_abs_negative,   max_suffix
    {"int",                4, true,  kSigned32Max,   kSigned32MaxAbsNeg, IntegerSuffix::Length::kInteger},
    {"unsigned",           4, false, kUnsigned32Max, kSigned32MaxAbsNeg, IntegerSuffix::Length::kInteger},
    {"long",               8, true,  kSigned64Max,   kSigned64MaxAbsNeg, IntegerSuffix::Length::kLong},
    {"unsigned long",      8, false, kUnsigned64Max, kSigned64MaxAbsNeg, IntegerSuffix::Length::kLong},
    {"long long",          8, true,  kSigned64Max,   kSigned64MaxAbsNeg, IntegerSuffix::Length::kLongLong},
    {"unsigned long long", 8, false, kUnsigned64Max, kSigned64MaxAbsNeg, IntegerSuffix::Length::kLongLong},
    // clang-format on
};

// Supports only base 2, 8, 10, and 16.
bool ValidForBase(IntegerPrefix::Base base, char c) {
  switch (base) {
    case IntegerPrefix::kBin:
      return c == '0' || c == '1';
    case IntegerPrefix::kOct:
      return c >= '0' && c <= '7';
    case IntegerPrefix::kDec:
      return c >= '0' && c <= '9';
    case IntegerPrefix::kHex:
      return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
  }
  return false;
}

// Returns the length of a <digits> sequence starting at the beginning of the input.
size_t GetDigitsLength(std::string_view input) {
  size_t result = 0;
  while (result < input.size() && isdigit(input[result]))
    result++;
  return result;
}

bool IsExponentCharacter(char c) { return c == 'e' || c == 'E'; }

bool IsSign(char c) { return c == '+' || c == '-'; }

}  // namespace

ErrOrValue StringToNumber(std::string_view str) {
  IntegerPrefix prefix = ExtractIntegerPrefix(&str);
  if (prefix.base == IntegerPrefix::kOct && prefix.octal_type == IntegerPrefix::OctalType::kC) {
    // Require "0o" prefixes for octal numbers instead of allowing C-style "0" prefixes. Octal
    // numbers are very unusual to be typed interactively in a debugger, and it's easier to
    // accidentally copy-and-paste a decimal number with a "0" at the beginning and get surprising
    // results. The "0o" format is used by Rust so we require it for clarity.
    return Err("Octal numbers must be prefixed with '0o'.");
  }

  auto suffix = ExtractIntegerSuffix(&str);
  if (suffix.has_error())
    return suffix.err();

  if (str.empty())
    return Err("Expected a number.");

  // Validate the characters in the number. This prevents strtoull from being too smart and trying
  // to handle prefixes itself.
  for (char c : str) {
    if (!ValidForBase(prefix.base, c))
      return Err("Invalid character in number.");
  }

  // strtoull doesn't take a const ending, but it doesn't modify the input.
  char* str_end = const_cast<char*>(str.end());
  char* parsed_end = str_end;

  // This will be the absolute value of the returned number.
  uint64_t abs_value = strtoull(str.data(), &parsed_end, static_cast<int>(prefix.base));

  // If strtoull stopped early it means it it hit an invalid character (shouldn't happen since we
  // validated above) or maybe the input was too long.
  if (parsed_end != str_end)
    return Err("Invalid number.");

  // Pick the smallest type that fits the data size as well as satisfies any suffixes.
  const TypeLookup* matched_type = nullptr;
  for (const auto& cur : kTypeLookup) {
    // Type must hold enough data.
    if (prefix.sign == IntegerPrefix::kNegative) {
      if (abs_value > cur.max_abs_negative)
        continue;
    } else {
      if (abs_value > cur.max_positive)
        continue;
    }

    if (static_cast<int>(cur.max_suffix) < static_cast<int>(suffix.value().length))
      continue;  // Requested length is larger.

    if (suffix.value().type_signed == IntegerSuffix::kUnsigned) {
      if (cur.type_signed)
        continue;  // Unsigned suffix requires unsigned type.
    } else if (prefix.sign == IntegerPrefix::kNegative && !cur.type_signed) {
      // Signed input requires a signed type unless a suffix overrode it which was checked above
      // ("-1u" should be unsigned).
      continue;
    }

    matched_type = &cur;
    break;
  }

  if (!matched_type) {
    // Anything not matched above will be an overflow. Put it into a unsigned 64-bit value and
    // tolerate the overflow.
    matched_type = &*(std::end(kTypeLookup) - 1);
  }

  int symbol_tag =
      matched_type->type_signed ? BaseType::kBaseTypeSigned : BaseType::kBaseTypeUnsigned;
  auto type =
      fxl::MakeRefCounted<BaseType>(symbol_tag, matched_type->byte_size, matched_type->name);

  uint64_t value = prefix.sign == IntegerPrefix::kNegative ? -abs_value : abs_value;

  // Construct the data. This assumes little-endian since it truncates or zero-fills off the right.
  std::vector<uint8_t> data(matched_type->byte_size);
  memcpy(&data[0], &value, matched_type->byte_size);

  return ExprValue(std::move(type), std::move(data));
}

IntegerPrefix ExtractIntegerPrefix(std::string_view* s) {
  IntegerPrefix prefix;
  if (s->empty())
    return prefix;  // Defaults OK for empty string.

  if ((*s)[0] == '-') {
    prefix.sign = IntegerPrefix::kNegative;

    // Allow whitespace between negative sign and the rest.
    size_t sign_len = 1;
    while (sign_len < s->size() && isspace((*s)[sign_len]))
      sign_len++;
    *s = s->substr(sign_len);
  }

  if (s->size() >= 2u && (*s)[0] == '0') {
    char second = (*s)[1];
    if (second == 'x' || second == 'X') {
      // Hex.
      *s = s->substr(2u);
      prefix.base = IntegerPrefix::kHex;
    } else if (second == 'b' || second == 'B') {
      // Binary.
      *s = s->substr(2u);
      prefix.base = IntegerPrefix::kBin;
    } else if (second == 'o' || second == 'O') {
      // Rust-style octal "0o".
      *s = s->substr(2u);
      prefix.base = IntegerPrefix::kOct;
      prefix.octal_type = IntegerPrefix::OctalType::kRust;
    } else {
      // Everything else beginning with a '0' is C-style octal. Note this requires >= 2 digits so
      // that "0" by itself is decimal.
      *s = s->substr(1u);
      prefix.base = IntegerPrefix::kOct;
      prefix.octal_type = IntegerPrefix::OctalType::kC;
    }
  }
  // Else case is decimal, doesn't need trimming, default is already correct.
  return prefix;
}

ErrOr<IntegerSuffix> ExtractIntegerSuffix(std::string_view* s) {
  IntegerSuffix suffix;

  // Check for any combination of "u" and either "l" or "ll". This works backwards to avoid two
  // passes since the suffix means the same in either order.
  bool have_unsigned = false;
  bool have_length = false;
  size_t suffix_begin = s->size();
  while (suffix_begin > 0) {
    char prev_char = (*s)[suffix_begin - 1];
    if (prev_char == 'U' || prev_char == 'u') {
      // Unsigned suffix.
      if (have_unsigned)
        return Err("Duplicate 'u' in number suffix.");
      have_unsigned = true;

      suffix.type_signed = IntegerSuffix::kUnsigned;
      suffix_begin--;
    } else if (prev_char == 'L' || prev_char == 'l') {
      // Suffix has an "l", disambiguate based on previous char.
      if (have_length)
        return Err("Duplicate 'l' or 'll' in number suffix.");
      have_length = true;

      // Technically C++ says "Ll" and "lL" aren't allowed, but we don't bother enforcing this.
      if (suffix_begin > 1 && ((*s)[suffix_begin - 2] == 'l' || (*s)[suffix_begin - 2] == 'L')) {
        // "ll" = Long long.
        suffix.length = IntegerSuffix::Length::kLongLong;
        suffix_begin -= 2;
      } else {
        // "l" by itself = Long.
        suffix.length = IntegerSuffix::Length::kLong;
        suffix_begin--;
      }
    } else {
      // Not a valid suffix number, stop.
      break;
    }
  }

  *s = s->substr(0, suffix_begin);
  return suffix;
}

// The floating-point format we expect is:
//
//   <float> := ( <significand> [<exponent>] [<suffix>] ) |
//              ( <digis> <exponent> [<suffix>] )
//
//   <significant> := ( <digits> "." <digits> ) |
//                    ( "." <digits> ) |
//                    ( <digits> "." )
//
//   <exponent> := ("e" | "E") [("+" | "-")] <digits>
//
//   <suffix> := "f" | "F" | "l" | "L"
//
// In other words, a floating point number must have either a "." or an "e", and a "." must have
// digits on at least one side of it.
//
// Rust requires that there be digits before a ".". This is important to disambiguate cases like
// "tuple.0" as being "tuple dot zero" from "tuple float-zero".
//
// TODO(bug 43220) Handle Rust-specific suffixes.
// TODO(bug 43222) Support C++17 hex floating point literals "0x342.1a"
size_t GetFloatTokenLength(ExprLanguage lang, std::string_view input) {
  std::string_view cur = input;

  // Digits before the dot.
  size_t before_dot = GetDigitsLength(cur);
  cur = cur.substr(before_dot);
  if (lang == ExprLanguage::kRust & before_dot == 0)
    return 0;

  // "."
  bool has_dot = false;
  if (!cur.empty() && cur[0] == '.') {
    has_dot = true;
    cur = cur.substr(1);
  }
  if (!before_dot && !has_dot)
    return 0;  // Must begin with digits or a dot to be a float.

  // Digits after the dot.
  size_t after_dot = GetDigitsLength(cur);
  cur = cur.substr(after_dot);
  if (has_dot && !before_dot && !after_dot)
    return 0;  // A dot must have digits on at least one side.

  // Optional exponent.
  bool has_exponent = false;
  if (!cur.empty() && IsExponentCharacter(cur[0])) {
    has_exponent = true;
    cur = cur.substr(1);

    if (!cur.empty() && IsSign(cur[0]))
      cur = cur.substr(1);  // Skip optional sign.

    size_t exponent = GetDigitsLength(cur);
    if (!exponent)
      return 0;  // Must have exponent digits to be a float.
    cur = cur.substr(exponent);
  }
  if (!has_dot && !has_exponent)
    return 0;  // Must have a dot or an exponend to be a float.

  // Consider all alphanumeric characters immediately following to be part of the token. This will
  // get any suffix characters but may get garbage also. The tokenizer isn't in charge of validating
  // floating point formatting, and something like "2.3hello" should be considered one invalid
  // floating-point token rather than a valid float followed by a valid identifier.
  while (!cur.empty() && isalnum(cur[0]))
    cur = cur.substr(1);

  return &cur[0] - &input[0];
}

FloatSuffix StripFloatSuffix(std::string_view* view) {
  if (view->empty())
    return FloatSuffix::kNone;

  char back = view->back();

  if (back == 'f' || back == 'F') {
    *view = view->substr(0, view->size() - 1);
    return FloatSuffix::kFloat;
  }

  if (back == 'l' || back == 'L') {
    *view = view->substr(0, view->size() - 1);
    return FloatSuffix::kLong;
  }

  return FloatSuffix::kNone;
}

ErrOrValue ValueForFloatToken(ExprLanguage lang, const ExprToken& token) {
  FXL_DCHECK(token.type() == ExprTokenType::kFloat);

  std::string_view value = token.value();
  FloatSuffix suffix = StripFloatSuffix(&value);
  if (lang != ExprLanguage::kC && suffix == FloatSuffix::kLong)
    suffix = FloatSuffix::kNone;  // Only C has a "long double" type.

  double_conversion::StringToDoubleConverter converter(0, 0.0, nan(""), nullptr, nullptr);

  fxl::RefPtr<BaseType> type;
  std::vector<uint8_t> data;

  int consumed = 0;
  switch (suffix) {
    case FloatSuffix::kNone: {
      double d = converter.StringToDouble(value.data(), static_cast<int>(value.size()), &consumed);
      data.resize(sizeof(double));
      memcpy(&data[0], &d, sizeof(double));
      type = GetBuiltinDoubleType(lang);
      break;
    }
    case FloatSuffix::kFloat: {
      float f = converter.StringToFloat(value.data(), static_cast<int>(value.size()), &consumed);
      data.resize(sizeof(float));
      memcpy(&data[0], &f, sizeof(float));
      type = GetBuiltinFloatType(lang);
      break;
    }
    case FloatSuffix::kLong: {
      // The parser doesn't support long doubles, but we can at least upcast if the local system
      // supports it.
      double d = converter.StringToDouble(value.data(), static_cast<int>(value.size()), &consumed);
      long double ld = d;
      data.resize(sizeof(long double));
      memcpy(&data[0], &ld, sizeof(long double));
      type = GetBuiltinLongDoubleType(lang);
      break;
    }
  }

  if (consumed != static_cast<int>(value.size()))
    return Err("Trailing characters on floating-point constant.");

  return ExprValue(std::move(type), std::move(data));
}

}  // namespace zxdb

// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/expr_token_type.h"

#include "src/developer/debug/zxdb/expr/expr_language.h"
#include "src/lib/fxl/arraysize.h"
#include "src/lib/fxl/logging.h"

namespace zxdb {

namespace {

// A constexpr version of isalnum.
constexpr bool IsAlnum(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
}

constexpr bool StringIsAlphanum(std::string_view str) {
  for (char c : str) {
    if (!IsAlnum(c))
      return false;
  }
  return true;
}

}  // namespace

// Must come before the tables below.
constexpr ExprTokenRecord::ExprTokenRecord(ExprTokenType t, unsigned langs,
                                           std::string_view static_val)
    : type(t),
      static_value(static_val),
      is_alphanum(StringIsAlphanum(static_val)),
      languages(langs) {}

namespace {

constexpr unsigned kLangAll =
    static_cast<unsigned>(ExprLanguage::kC) | static_cast<unsigned>(ExprLanguage::kRust);
constexpr unsigned kLangC = static_cast<unsigned>(ExprLanguage::kC);
constexpr unsigned kLangRust = static_cast<unsigned>(ExprLanguage::kRust);

// Note that we allow a number of things like "sizeof" in Rust as well because
// there are no good alternatives and these constructs can be useful. We may
// consider replacing them with a more Rust-like construct in the future.
constexpr ExprTokenRecord kRecords[kNumExprTokenTypes] = {
    // clang-format off
    {ExprTokenType::kInvalid,         0},
    {ExprTokenType::kName,            kLangAll},
    {ExprTokenType::kInteger,         kLangAll},
    {ExprTokenType::kEquals,          kLangAll,  "="},
    {ExprTokenType::kEquality,        kLangAll,  "=="},
    {ExprTokenType::kInequality,      kLangAll,  "!="},
    {ExprTokenType::kLessEqual,       kLangAll,  "<="},
    {ExprTokenType::kGreaterEqual,    kLangAll,  ">="},
    {ExprTokenType::kSpaceship,       kLangAll,  "<=>"},
    {ExprTokenType::kDot,             kLangAll,  "."},
    {ExprTokenType::kComma,           kLangAll,  ","},
    {ExprTokenType::kSemicolon,       kLangAll,  ";"},
    {ExprTokenType::kStar,            kLangAll,  "*"},
    {ExprTokenType::kAmpersand,       kLangAll,  "&"},
    {ExprTokenType::kDoubleAnd,       kLangAll,  "&&"},
    {ExprTokenType::kBitwiseOr,       kLangAll,  "|"},
    {ExprTokenType::kLogicalOr,       kLangAll,  "||"},
    {ExprTokenType::kArrow,           kLangAll,  "->"},
    {ExprTokenType::kLeftSquare,      kLangAll,  "["},
    {ExprTokenType::kRightSquare,     kLangAll,  "]"},
    {ExprTokenType::kLeftParen,       kLangAll,  "("},
    {ExprTokenType::kRightParen,      kLangAll,  ")"},
    {ExprTokenType::kLess,            kLangAll,  "<"},
    {ExprTokenType::kGreater,         kLangAll,  ">"},
    {ExprTokenType::kMinus,           kLangAll,  "-"},
    {ExprTokenType::kBang,            kLangAll,  "!"},
    {ExprTokenType::kPlus,            kLangAll,  "+"},
    {ExprTokenType::kSlash,           kLangAll,  "/"},
    {ExprTokenType::kCaret,           kLangAll,  "^"},
    {ExprTokenType::kPercent,         kLangAll,  "%"},
    {ExprTokenType::kColonColon,      kLangAll,  "::"},
    {ExprTokenType::kShiftLeft,       kLangAll,  "<<"},
    {ExprTokenType::kShiftRight,      0},  // See header.
    {ExprTokenType::kTrue,            kLangAll,  "true"},
    {ExprTokenType::kFalse,           kLangAll,  "false"},
    {ExprTokenType::kConst,           kLangAll,  "const"},
    {ExprTokenType::kMut,             kLangRust, "mut"},
    {ExprTokenType::kVolatile,        kLangC,    "volatile"},
    {ExprTokenType::kRestrict,        kLangC,    "restrict"},
    {ExprTokenType::kReinterpretCast, kLangC,    "reinterpret_cast"},
    {ExprTokenType::kStaticCast,      kLangC,    "static_cast"},
    {ExprTokenType::kSizeof,          kLangAll,  "sizeof"},
    {ExprTokenType::kAs,              kLangRust, "as"},
    // clang-format on
};

}  // namespace

const ExprTokenRecord& RecordForTokenType(ExprTokenType type) {
  static_assert(arraysize(kRecords) == static_cast<int>(ExprTokenType::kNumTypes),
                "kRecords needs updating to match ExprTokenType");

  // Checks that this record is in the right place.
  FXL_DCHECK(kRecords[static_cast<size_t>(type)].type == type);

  return kRecords[static_cast<size_t>(type)];
}

}  // namespace zxdb

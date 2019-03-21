// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/expr/expr_token_type.h"

#include "lib/fxl/arraysize.h"
#include "lib/fxl/logging.h"

namespace zxdb {

namespace {

// A constexpr version of isalnum.
constexpr bool IsAlnum(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9');
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
constexpr ExprTokenRecord::ExprTokenRecord(ExprTokenType t,
                                           std::string_view static_val)
    : type(t),
      static_value(static_val),
      is_alphanum(StringIsAlphanum(static_val)) {}

namespace {

constexpr ExprTokenRecord kRecords[kNumExprTokenTypes] = {
    // clang-format off
    {ExprTokenType::kInvalid},
    {ExprTokenType::kName},
    {ExprTokenType::kInteger},
    {ExprTokenType::kEquals,          "="},
    {ExprTokenType::kEquality,        "=="},
    {ExprTokenType::kDot,             "."},
    {ExprTokenType::kComma,           ","},
    {ExprTokenType::kStar,            "*"},
    {ExprTokenType::kAmpersand,       "&"},
    {ExprTokenType::kDoubleAnd,       "&&"},
    {ExprTokenType::kBitwiseOr,       "|"},
    {ExprTokenType::kLogicalOr,       "||"},
    {ExprTokenType::kArrow,           "->"},
    {ExprTokenType::kLeftSquare,      "["},
    {ExprTokenType::kRightSquare,     "]"},
    {ExprTokenType::kLeftParen,       "("},
    {ExprTokenType::kRightParen,      ")"},
    {ExprTokenType::kLess,            "<"},
    {ExprTokenType::kGreater,         ">"},
    {ExprTokenType::kMinus,           "-"},
    {ExprTokenType::kPlus,            "+"},
    {ExprTokenType::kColonColon,      "::"},
    {ExprTokenType::kTrue,            "true"},
    {ExprTokenType::kFalse,           "false"},
    {ExprTokenType::kConst,           "const"},
    {ExprTokenType::kVolatile,        "volatile"},
    {ExprTokenType::kRestrict,        "restrict"},
    {ExprTokenType::kReinterpretCast, "reinterpret_cast"},
    // clang-format on
};

}  // namespace

const ExprTokenRecord& RecordForTokenType(ExprTokenType type) {
  static_assert(
      arraysize(kRecords) == static_cast<int>(ExprTokenType::kNumTypes),
      "kRecords needs updating to match ExprTokenType");

  // Checks that this record is in the right place.
  FXL_DCHECK(kRecords[static_cast<size_t>(type)].type == type);

  return kRecords[static_cast<size_t>(type)];
}

}  // namespace zxdb

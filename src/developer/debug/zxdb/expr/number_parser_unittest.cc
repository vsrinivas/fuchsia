// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/expr/number_parser.h"

#include <string.h>

#include <variant>

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/expr/expr_token.h"
#include "src/developer/debug/zxdb/expr/expr_value.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/lib/fxl/arraysize.h"

namespace zxdb {

TEST(NumberParser, ExtractIntegerPrefix) {
  struct PrefixCase {
    const char* input;  // Input with prefix.
    const char* num;    // Number without prefix.
    IntegerPrefix::Sign sign;
    IntegerPrefix::Base base;
    IntegerPrefix::OctalType oct;
  } kCases[] = {
      // clang-format off
      // input   num      sign                      base                 oct
      {"",       "",      IntegerPrefix::kPositive, IntegerPrefix::kDec, IntegerPrefix::OctalType::kC},
      {"-",      "",      IntegerPrefix::kNegative, IntegerPrefix::kDec, IntegerPrefix::OctalType::kC},
      {"76",     "76",    IntegerPrefix::kPositive, IntegerPrefix::kDec, IntegerPrefix::OctalType::kC},
      {"- 76",   "76",    IntegerPrefix::kNegative, IntegerPrefix::kDec, IntegerPrefix::OctalType::kC},
      {"0b101",  "101",   IntegerPrefix::kPositive, IntegerPrefix::kBin, IntegerPrefix::OctalType::kC},
      {"-0b101", "101",   IntegerPrefix::kNegative, IntegerPrefix::kBin, IntegerPrefix::OctalType::kC},
      {"0xabc",  "abc",   IntegerPrefix::kPositive, IntegerPrefix::kHex, IntegerPrefix::OctalType::kC},
      {"0o123",  "123",   IntegerPrefix::kPositive, IntegerPrefix::kOct, IntegerPrefix::OctalType::kRust},
      {"0",      "0",     IntegerPrefix::kPositive, IntegerPrefix::kDec, IntegerPrefix::OctalType::kC},
      {"-\t0",   "0",     IntegerPrefix::kNegative, IntegerPrefix::kDec, IntegerPrefix::OctalType::kC},
      {"hello",  "hello", IntegerPrefix::kPositive, IntegerPrefix::kDec, IntegerPrefix::OctalType::kC},
      // clang-format on
  };

  for (const auto& cur : kCases) {
    std::string_view view(cur.input);
    IntegerPrefix prefix = ExtractIntegerPrefix(&view);

    EXPECT_EQ(0, view.compare(cur.num)) << "Input " << cur.input;
    EXPECT_EQ(cur.sign, prefix.sign) << "Input " << cur.input;
    EXPECT_EQ(cur.base, prefix.base) << "Input " << cur.input;
    if (cur.base == IntegerPrefix::kOct)
      EXPECT_EQ(cur.oct, prefix.octal_type) << "Input " << cur.input;
  }
}

TEST(NumberParser, ExtractIntegerSuffix) {
  struct SuffixCase {
    const char* input;    // Input with suffix.
    const char* err_msg;  // Null for no error.
    const char* num;      // Number without suffix (when no error).
    IntegerSuffix::Signed sign;
    IntegerSuffix::Length length;
  } kCases[] = {
      // clang-format off
      // input  err_msg  num     sign                      length
      {"",      nullptr, "",     IntegerSuffix::kSigned,   IntegerSuffix::Length::kInteger},
      {"1234",  nullptr, "1234", IntegerSuffix::kSigned,   IntegerSuffix::Length::kInteger},
      {"12l",   nullptr, "12",   IntegerSuffix::kSigned,   IntegerSuffix::Length::kLong},
      {"12LL",  nullptr, "12",   IntegerSuffix::kSigned,   IntegerSuffix::Length::kLongLong},
      {"13u",   nullptr, "13",   IntegerSuffix::kUnsigned, IntegerSuffix::Length::kInteger},
      {"14ul",  nullptr, "14",   IntegerSuffix::kUnsigned, IntegerSuffix::Length::kLong},
      {"15LU",  nullptr, "15",   IntegerSuffix::kUnsigned, IntegerSuffix::Length::kLong},
      {"16Llu", nullptr, "16",   IntegerSuffix::kUnsigned, IntegerSuffix::Length::kLongLong},
      {"17ulL", nullptr, "17",   IntegerSuffix::kUnsigned, IntegerSuffix::Length::kLongLong},

      // Bad number (still extract suffix).
      {"taLl",  nullptr, "ta",   IntegerSuffix::kSigned,   IntegerSuffix::Length::kLongLong},

      // Error cases.
      {"12lul", "Duplicate 'l' or 'll' in number suffix.", "", IntegerSuffix::kSigned, IntegerSuffix::Length::kLong},
      {"12uu", "Duplicate 'u' in number suffix.", "", IntegerSuffix::kSigned, IntegerSuffix::Length::kLong},
      // clang-format on
  };

  for (const auto& cur : kCases) {
    std::string_view view(cur.input);

    ErrOr<IntegerSuffix> result = ExtractIntegerSuffix(&view);
    if (cur.err_msg) {
      // Expected error.
      ASSERT_TRUE(result.has_error());
      EXPECT_EQ(cur.err_msg, result.err().msg());
    } else {
      // No expected error.
      ASSERT_FALSE(result.has_error()) << result.err().msg();
      EXPECT_EQ(0, view.compare(cur.num)) << "For input " << cur.input;
      EXPECT_EQ(cur.sign, result.value().type_signed) << "For input " << cur.input;
      EXPECT_EQ(cur.length, result.value().length) << "For input " << cur.input;
    }
  }
}

TEST(NumberParser, StringToNumber) {
  using ExpectedType = std::variant<int32_t, uint32_t, int64_t, uint64_t>;

  struct Case {
    const char* input;    // Input with suffix.
    const char* err_msg;  // Null for no error.

    ExpectedType expected;
    const char* expected_type_name;
  } kCases[] = {
      // Normal positive input.
      {"0", nullptr, int32_t(0), "int"},
      {"23", nullptr, int32_t(23), "int"},
      {"23u", nullptr, uint32_t(23), "unsigned"},
      {"23l", nullptr, int64_t(23), "long"},
      {"23ul", nullptr, uint64_t(23), "unsigned long"},
      {"23ll", nullptr, int64_t(23), "long long"},
      {"23ull", nullptr, uint64_t(23), "unsigned long long"},

      // Normal negative input.
      {"-0", nullptr, int32_t(0), "int"},
      {"-23", nullptr, int32_t(-23), "int"},
      {"-23u", nullptr, uint32_t(-23), "unsigned"},
      {"-23l", nullptr, int64_t(-23), "long"},
      {"-23lu", nullptr, uint64_t(-23), "unsigned long"},
      {"-23ll", nullptr, int64_t(-23), "long long"},
      {"-23llu", nullptr, uint64_t(-23), "unsigned long long"},

      // Hex input.
      {"0xabcd", nullptr, int32_t(0xabcd), "int"},
      {"- 0x614u", nullptr, uint32_t(-0x614), "unsigned"},
      {"0xabcdull", nullptr, uint64_t(0xabcd), "unsigned long long"},
      {"0xffffFFFFffffFFFF", nullptr, uint64_t(0xffffFFFFffffFFFF), "unsigned long"},
      // This overflow case gets promoted to "long long" because it's "bigger". C++ would put this
      // in a long.
      {"-0xffffFFFFffffFFFF", nullptr, uint64_t(-0xffffFFFFffffFFFF), "unsigned long long"},

      // Octal input ("0" prefix disallowed).
      {"0o567", nullptr, int32_t(0567), "int"},
      {"-0o567", nullptr, int32_t(-0567), "int"},
      {"-0o0567llu", nullptr, uint64_t(-0567), "unsigned long long"},
      {"0567", "Octal numbers must be prefixed with '0o'.", int32_t(0567), "int"},
      {"-0567llu", "Octal numbers must be prefixed with '0o'.", uint64_t(-0567),
       "unsigned long long"},

      // Binary input.
      {"0b0", nullptr, int32_t(0), "int"},
      {"0b110010", nullptr, int32_t(0b110010), "int"},
      {"0b110010l", nullptr, int64_t(0b110010), "long"},
      {"-0b110010l", nullptr, int64_t(-0b110010), "long"},

      // This number is one too large to put in an signed 32-bit type. C++ would expand this to a
      // "long" unless it was in a non-decimal base, but our simpler rules put it in an unsigned
      // since it fits.
      {"2147483648", nullptr, uint32_t(2147483648), "unsigned"},
      // Long override makes it more clear.
      {"2147483648l", nullptr, int64_t(2147483648), "long"},

      // Largest 32-bit negative number. Forcing it to unsigned gives the same bit pattern and same
      // size type, but in an unsigned type.
      {"-2147483648", nullptr, int32_t(-2147483648), "int"},
      {"-2147483648u", nullptr, uint32_t(-2147483648u), "unsigned"},

      // Some error cases.
      {"", "Expected a number.", 0, nullptr},
      {"0x56g", "Invalid character in number.", 0, nullptr},
      {"0x56 56", "Invalid character in number.", 0, nullptr},
      {"0b5", "Invalid character in number.", 0, nullptr},
      {"0x0x34", "Invalid character in number.", 0, nullptr},
      {"--45", "Invalid character in number.", 0, nullptr},
      {"67lll", "Duplicate 'l' or 'll' in number suffix.", 0, nullptr},
  };

  for (const auto& cur : kCases) {
    ErrOrValue result = StringToNumber(cur.input);

    if (cur.err_msg) {
      // Expected failure.
      ASSERT_TRUE(result.has_error()) << " Input = " << cur.input;
      EXPECT_EQ(cur.err_msg, result.err().msg()) << "Input = " << cur.input;
    } else {
      // Expected success.
      ASSERT_FALSE(result.has_error()) << result.err().msg() << " for input = " << cur.input;

      // This craziness calls the lambda with the given expected type and value. We can compare the
      // type of the expected parameter based on the test case with the type generated by our code
      // in the expr_value.
      std::visit(
          [&result, cur](auto expected) {
            using ExpectedType = std::decay_t<decltype(expected)>;

            const BaseType* expr_type = result.value().type()->AsBaseType();
            ASSERT_TRUE(expr_type) << "Input = " << cur.input;

            // Size of types should match.
            EXPECT_EQ(sizeof(ExpectedType), result.value().data().size())
                << "Input = " << cur.input;

            // Sign on types should match.
            if (std::is_signed_v<ExpectedType>) {
              EXPECT_EQ(expr_type->base_type(), BaseType::kBaseTypeSigned)
                  << "Input = " << cur.input;
            } else {
              EXPECT_EQ(expr_type->base_type(), BaseType::kBaseTypeUnsigned)
                  << "Input = " << cur.input;
            }

            // Names of types should match.
            EXPECT_EQ(cur.expected_type_name, expr_type->GetFullName()) << "Input = " << cur.input;

            // Values should match.
            EXPECT_EQ(expected, result.value().GetAs<ExpectedType>()) << "Input = " << cur.input;
          },
          cur.expected);
    }
  }
}

TEST(NumberParser, GetFloatTokenLength) {
  EXPECT_EQ(0u, GetFloatTokenLength(ExprLanguage::kC, std::string_view()));
  EXPECT_EQ(0u, GetFloatTokenLength(ExprLanguage::kC, " 2.3"));   // Whitespace doesn't count.
  EXPECT_EQ(0u, GetFloatTokenLength(ExprLanguage::kC, "12"));     // Integer, not a float.
  EXPECT_EQ(0u, GetFloatTokenLength(ExprLanguage::kC, "-12.2"));  // Signs not counted.
  EXPECT_EQ(0u, GetFloatTokenLength(ExprLanguage::kC, "12foo"));  // Not a number.

  // Simple valid cases.
  EXPECT_EQ(3u, GetFloatTokenLength(ExprLanguage::kC, "2.3"));
  EXPECT_EQ(3u, GetFloatTokenLength(ExprLanguage::kC, "2.3"));
  EXPECT_EQ(2u, GetFloatTokenLength(ExprLanguage::kC, "2. foo"));
  EXPECT_EQ(3u, GetFloatTokenLength(ExprLanguage::kC, "12.+float"));

  // Exponents.
  EXPECT_EQ(0u, GetFloatTokenLength(ExprLanguage::kC, "12e"));  // Exponent needs digits.
  EXPECT_EQ(0u, GetFloatTokenLength(ExprLanguage::kC, "12extremely"));
  EXPECT_EQ(0u, GetFloatTokenLength(ExprLanguage::kC, "12e+"));  // Exponent needs digits.
  EXPECT_EQ(5u, GetFloatTokenLength(ExprLanguage::kC, "12e12"));
  EXPECT_EQ(6u, GetFloatTokenLength(ExprLanguage::kC, "12.e12 "));
  EXPECT_EQ(9u, GetFloatTokenLength(ExprLanguage::kC, "12.019e12+aa"));
  EXPECT_EQ(5u, GetFloatTokenLength(ExprLanguage::kC,
                                    "12e12.2"));  // ".2" not counted as part of the number.
  EXPECT_EQ(0u, GetFloatTokenLength(ExprLanguage::kC, "12e+"));
  EXPECT_EQ(6u, GetFloatTokenLength(ExprLanguage::kC, "12e+12"));
  EXPECT_EQ(6u, GetFloatTokenLength(ExprLanguage::kC, "12E-12"));
  EXPECT_EQ(0u, GetFloatTokenLength(ExprLanguage::kC, "12 e-12"));
  EXPECT_EQ(0u, GetFloatTokenLength(ExprLanguage::kC, "12e- 12"));

  // Suffixes. A valid number with any alphanumeric characters following is included. The suffixed
  // will be validated in the parsing phase, not this tokenizing phase.
  // TODO(bug 43220) Handle Rust-specific suffixes.
  EXPECT_EQ(4u, GetFloatTokenLength(ExprLanguage::kC, "12.f"));
  EXPECT_EQ(8u, GetFloatTokenLength(ExprLanguage::kC, "12.float"));
  EXPECT_EQ(14u, GetFloatTokenLength(ExprLanguage::kC, "12e-12nonsense"));

  // Rust requires a leading digit, C doesn't (as long as there's a digit following);
  EXPECT_EQ(3u, GetFloatTokenLength(ExprLanguage::kC, ".14"));
  EXPECT_EQ(0u, GetFloatTokenLength(ExprLanguage::kC, ".e12"));
  EXPECT_EQ(0u, GetFloatTokenLength(ExprLanguage::kRust, ".14"));
}

TEST(NumberParser, StripFloatSuffix) {
  std::string_view input;
  EXPECT_EQ(FloatSuffix::kNone, StripFloatSuffix(&input));

  input = "2.4";
  EXPECT_EQ(FloatSuffix::kNone, StripFloatSuffix(&input));
  EXPECT_EQ(input, "2.4");

  input = "2.F";
  EXPECT_EQ(FloatSuffix::kFloat, StripFloatSuffix(&input));
  EXPECT_EQ(input, "2.");

  // Extra characters are tolerated, these will be identified when parsed.
  input = "2.4e7randomf";
  EXPECT_EQ(FloatSuffix::kFloat, StripFloatSuffix(&input));
  EXPECT_EQ(input, "2.4e7random");

  input = "9L";
  EXPECT_EQ(FloatSuffix::kLong, StripFloatSuffix(&input));
  EXPECT_EQ(input, "9");

  input = "l";
  EXPECT_EQ(FloatSuffix::kLong, StripFloatSuffix(&input));
  EXPECT_EQ(input, "");
}

namespace {

template <typename FloatType>
bool FloatValuesEqual(ExprLanguage lang, const char* input, const char* expected_type,
                      FloatType expected_value) {
  ExprToken token(ExprTokenType::kFloat, input, 0);

  ErrOrValue result = ValueForFloatToken(lang, token);
  EXPECT_TRUE(result.ok()) << result.err().msg();
  if (!result.ok())
    return false;

  EXPECT_EQ(expected_type, result.value().type()->GetFullName());
  EXPECT_EQ(sizeof(expected_value), result.value().data().size());
  if (sizeof(expected_value) != result.value().data().size())
    return false;

  // Say the float converter makes the same floating-point value as the current compiler. This
  // should be the case for all sane compilers.
  FloatType result_float;
  memcpy(&result_float, &result.value().data()[0], sizeof(FloatType));
  EXPECT_EQ(expected_value, result_float);
  return expected_value == result_float;
}

}  // namespace

TEST(NumberParser, ValueForFloatToken) {
  EXPECT_TRUE(FloatValuesEqual(ExprLanguage::kC, "2.3", "double", 2.3));
  EXPECT_TRUE(FloatValuesEqual(ExprLanguage::kC, "3.14e+0f", "float", 3.14f));
  EXPECT_TRUE(FloatValuesEqual(ExprLanguage::kC, "2e9", "double", 2e9));

  EXPECT_TRUE(FloatValuesEqual(ExprLanguage::kRust, ".3", "f64", .3));
  EXPECT_TRUE(FloatValuesEqual(ExprLanguage::kRust, "3.", "f64", 3.));
  // Technically this line is wrong. It should be "2e9f32".
  // TODO(bug 43220) Handle Rust-specific suffixes.
  EXPECT_TRUE(FloatValuesEqual(ExprLanguage::kRust, "2e9f", "f32", 2e9f));

  // Some error cases.
  const char kTrailingMsg[] = "Trailing characters on floating-point constant.";
  EXPECT_EQ(kTrailingMsg,
            ValueForFloatToken(ExprLanguage::kC, ExprToken(ExprTokenType::kFloat, "3blah", 0))
                .err()
                .msg());
  EXPECT_EQ(kTrailingMsg,
            ValueForFloatToken(ExprLanguage::kC, ExprToken(ExprTokenType::kFloat, "3.2.3", 0))
                .err()
                .msg());
}

}  // namespace zxdb

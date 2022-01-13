// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/stdcompat/array.h>

#include <cstdint>

#include <pretty/cpp/sizes.h>
#include <zxtest/zxtest.h>

using pretty::FormattedBytes;
using pretty::SizeUnit;

TEST(CppSizeTest, Empty) {
  EXPECT_STREQ("", FormattedBytes().str());
  EXPECT_STREQ("", FormattedBytes().c_str());
  EXPECT_STREQ("", FormattedBytes().Magnitude());
  EXPECT_EQ(SizeUnit::kAuto, FormattedBytes().Unit());
}

TEST(CppSizeTest, Simple) {
  EXPECT_STREQ("0B", FormattedBytes(0).str());
  EXPECT_STREQ("0B", FormattedBytes(0).c_str());
  EXPECT_STREQ("0", FormattedBytes(0).Magnitude());
  EXPECT_EQ(SizeUnit::kBytes, FormattedBytes(0).Unit());

  EXPECT_STREQ("1B", FormattedBytes(1).str());
  EXPECT_STREQ("1B", FormattedBytes(1).c_str());
  EXPECT_STREQ("1", FormattedBytes(1).Magnitude());
  EXPECT_EQ(SizeUnit::kBytes, FormattedBytes(1).Unit());

  EXPECT_STREQ("1K", FormattedBytes(1024).str());
  EXPECT_STREQ("1K", FormattedBytes(1024).c_str());
  EXPECT_STREQ("1", FormattedBytes(1024).Magnitude());
  EXPECT_EQ(SizeUnit::kKiB, FormattedBytes(1024).Unit());

  EXPECT_STREQ("9.8K", FormattedBytes(10000).str());
  EXPECT_STREQ("9.8K", FormattedBytes(10000).c_str());
  EXPECT_STREQ("9.8", FormattedBytes(10000).Magnitude());
  EXPECT_EQ(SizeUnit::kKiB, FormattedBytes(10000).Unit());

  EXPECT_STREQ("18446744073709551615B", FormattedBytes(UINT64_MAX, SizeUnit::kBytes).str());
  EXPECT_STREQ("18446744073709551615B", FormattedBytes(UINT64_MAX, SizeUnit::kBytes).c_str());
  EXPECT_STREQ("18446744073709551615", FormattedBytes(UINT64_MAX, SizeUnit::kBytes).Magnitude());
  EXPECT_EQ(SizeUnit::kBytes, FormattedBytes(UINT64_MAX, SizeUnit::kBytes).Unit());
}

TEST(CppSizeTest, Copy) {
  FormattedBytes empty;
  empty = FormattedBytes(1);
  EXPECT_STREQ("1B", empty.str());
  EXPECT_STREQ("1B", empty.c_str());

  FormattedBytes copy(FormattedBytes(2));
  EXPECT_STREQ("2B", copy.str());
  EXPECT_STREQ("2B", copy.c_str());
}

TEST(CppSizeTest, SetSize) {
  FormattedBytes val;
  EXPECT_STREQ("", val.str());
  EXPECT_STREQ("", val.c_str());
  val.SetSize(2).SetSize(1);
  EXPECT_STREQ("1B", val.str());
  EXPECT_STREQ("1B", val.c_str());
  val.SetSize(10000);
  EXPECT_STREQ("9.8K", val.str());
  EXPECT_STREQ("9.8K", val.c_str());
  val.SetSize(10000, SizeUnit::kBytes);
  EXPECT_STREQ("10000B", val.str());
  val.SetSize(20000, SizeUnit::kBytes).SetSize(1);
  val.SetSize(17).SetSize(30000, SizeUnit::kBytes);
  EXPECT_STREQ("30000B", val.c_str());
}

TEST(CppSizeTest, ToString) {
  constexpr std::array kAllSizeUnits = {
      SizeUnit::kAuto, SizeUnit::kBytes, SizeUnit::kKiB, SizeUnit::kMiB,
      SizeUnit::kGiB,  SizeUnit::kTiB,   SizeUnit::kPiB, SizeUnit::kEiB,
  };
  for (SizeUnit unit : kAllSizeUnits) {
    switch (unit) {
      case SizeUnit::kAuto:
        EXPECT_TRUE(FormattedBytes::ToString(unit).empty());
        break;
      case SizeUnit::kBytes:
      case SizeUnit::kKiB:
      case SizeUnit::kMiB:
      case SizeUnit::kGiB:
      case SizeUnit::kTiB:
      case SizeUnit::kPiB:
      case SizeUnit::kEiB: {
        std::string_view str = FormattedBytes::ToString(unit);
        ASSERT_EQ(1, str.size());
        EXPECT_EQ(static_cast<char>(unit), str.front());
      }
    }
  }
}

TEST(CppSizeTest, ParseSizeFromFormattedString) {
  struct FormattedTestCases {
    const size_t expected_bytes;
    std::string_view input;
  };

  constexpr uint64_t kKilo = static_cast<uint64_t>(1) << 10;
  constexpr uint64_t kMega = static_cast<uint64_t>(1) << 20;
  constexpr uint64_t kGiga = static_cast<uint64_t>(1) << 30;
  constexpr uint64_t kTera = static_cast<uint64_t>(1) << 40;
  constexpr uint64_t kPeta = static_cast<uint64_t>(1) << 50;
  constexpr uint64_t kExa = static_cast<uint64_t>(1) << 60;

  auto test_cases = cpp20::to_array<FormattedTestCases>({
      // Integral
      {1234, "1234"},
      {1234, "1234b"},
      {1234, "1234B"},
      {1234 * kKilo, "1234k"},
      {1234 * kKilo, "1234K"},
      {1234 * kMega, "1234m"},
      {1234 * kMega, "1234M"},
      {1234 * kGiga, "1234g"},
      {1234 * kGiga, "1234G"},
      {1234 * kTera, "1234t"},
      {1234 * kTera, "1234T"},
      {5 * kPeta, "5p"},
      {5 * kPeta, "5P"},
      {2 * kExa, "2e"},
      {2 * kExa, "2E"},

      // Fractional
      {10700, "10.4492187500k"},
      {10700, "10.4492187500K"},
      {10700 * kKilo, "10.4492187500m"},
      {10700 * kKilo, "10.4492187500M"},
      {10700 * kMega, "10.4492187500g"},
      {10700 * kMega, "10.4492187500G"},
      {10700 * kGiga, "10.4492187500t"},
      {10700 * kGiga, "10.4492187500T"},
      {10700 * kTera, "10.4492187500p"},
      {10700 * kTera, "10.4492187500P"},
      {1441151880758558720, "1.25e"},
      {1441151880758558720, "1.25E"},
  });
  // Reuse the test cases that check byte to string conversion,
  // just swap input and output.
  for (auto test_case : test_cases) {
    auto size_or = pretty::ParseSizeBytes(test_case.input);
    ASSERT_TRUE(size_or.has_value(), "%.*s", static_cast<int>(test_case.input.size()),
                test_case.input.data());
    EXPECT_EQ(*size_or, test_case.expected_bytes, "%.*s", static_cast<int>(test_case.input.size()),
              test_case.input.data());
  }
}

TEST(CppSizeTest, ParseSizeFromWithInvalidInputs) {
  // Reuse the test cases that check byte to string conversion,
  // just swap input and output.
  auto invalid_inputs =
      cpp20::to_array<std::string_view>({{}, "", "1..1", "1w", "b", "AM", "1.AM", "A.1M"});

  for (auto invalid_input : invalid_inputs) {
    EXPECT_FALSE(pretty::ParseSizeBytes(invalid_input).has_value(), "%.*s",
                 static_cast<int>(invalid_input.size()), invalid_input.data());
  }
}

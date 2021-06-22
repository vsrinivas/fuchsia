// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <pretty/cpp/sizes.h>
#include <zxtest/zxtest.h>

using pretty::FormattedBytes;
using pretty::SizeUnit;

TEST(CppSizeTest, Empty) {
  EXPECT_STR_EQ("", FormattedBytes().str());
  EXPECT_STR_EQ("", FormattedBytes().c_str());
  EXPECT_STR_EQ("", FormattedBytes().Magnitude());
  EXPECT_EQ(SizeUnit::kAuto, FormattedBytes().Unit());
}

TEST(CppSizeTest, Simple) {
  EXPECT_STR_EQ("0B", FormattedBytes(0).str());
  EXPECT_STR_EQ("0B", FormattedBytes(0).c_str());
  EXPECT_STR_EQ("0", FormattedBytes(0).Magnitude());
  EXPECT_EQ(SizeUnit::kBytes, FormattedBytes(0).Unit());

  EXPECT_STR_EQ("1B", FormattedBytes(1).str());
  EXPECT_STR_EQ("1B", FormattedBytes(1).c_str());
  EXPECT_STR_EQ("1", FormattedBytes(1).Magnitude());
  EXPECT_EQ(SizeUnit::kBytes, FormattedBytes(1).Unit());

  EXPECT_STR_EQ("1k", FormattedBytes(1024).str());
  EXPECT_STR_EQ("1k", FormattedBytes(1024).c_str());
  EXPECT_STR_EQ("1", FormattedBytes(1024).Magnitude());
  EXPECT_EQ(SizeUnit::kKiB, FormattedBytes(1024).Unit());

  EXPECT_STR_EQ("9.8k", FormattedBytes(10000).str());
  EXPECT_STR_EQ("9.8k", FormattedBytes(10000).c_str());
  EXPECT_STR_EQ("9.8", FormattedBytes(10000).Magnitude());
  EXPECT_EQ(SizeUnit::kKiB, FormattedBytes(10000).Unit());

  EXPECT_STR_EQ("18446744073709551615B", FormattedBytes(UINT64_MAX, SizeUnit::kBytes).str());
  EXPECT_STR_EQ("18446744073709551615B", FormattedBytes(UINT64_MAX, SizeUnit::kBytes).c_str());
  EXPECT_STR_EQ("18446744073709551615", FormattedBytes(UINT64_MAX, SizeUnit::kBytes).Magnitude());
  EXPECT_EQ(SizeUnit::kBytes, FormattedBytes(UINT64_MAX, SizeUnit::kBytes).Unit());
}

TEST(CppSizeTest, Copy) {
  FormattedBytes empty;
  empty = FormattedBytes(1);
  EXPECT_STR_EQ("1B", empty.str());
  EXPECT_STR_EQ("1B", empty.c_str());

  FormattedBytes copy(FormattedBytes(2));
  EXPECT_STR_EQ("2B", copy.str());
  EXPECT_STR_EQ("2B", copy.c_str());
}

TEST(CppSizeTest, SetSize) {
  FormattedBytes val;
  EXPECT_STR_EQ("", val.str());
  EXPECT_STR_EQ("", val.c_str());
  val.SetSize(2).SetSize(1);
  EXPECT_STR_EQ("1B", val.str());
  EXPECT_STR_EQ("1B", val.c_str());
  val.SetSize(10000);
  EXPECT_STR_EQ("9.8k", val.str());
  EXPECT_STR_EQ("9.8k", val.c_str());
  val.SetSize(10000, SizeUnit::kBytes);
  EXPECT_STR_EQ("10000B", val.str());
  val.SetSize(20000, SizeUnit::kBytes).SetSize(1);
  val.SetSize(17).SetSize(30000, SizeUnit::kBytes);
  EXPECT_STR_EQ("30000B", val.c_str());
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

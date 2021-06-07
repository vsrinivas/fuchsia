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
}

TEST(CppSizeTest, Simple) {
  EXPECT_STR_EQ("0B", FormattedBytes(0).str());
  EXPECT_STR_EQ("0B", FormattedBytes(0).c_str());
  EXPECT_STR_EQ("1B", FormattedBytes(1).str());
  EXPECT_STR_EQ("1B", FormattedBytes(1).c_str());
  EXPECT_STR_EQ("1k", FormattedBytes(1024).str());
  EXPECT_STR_EQ("1k", FormattedBytes(1024).c_str());
  EXPECT_STR_EQ("18446744073709551615B", FormattedBytes(UINT64_MAX, SizeUnit::kBytes).str());
  EXPECT_STR_EQ("18446744073709551615B", FormattedBytes(UINT64_MAX, SizeUnit::kBytes).c_str());
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

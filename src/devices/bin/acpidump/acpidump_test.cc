// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "acpidump.h"

#include <initializer_list>
#include <vector>

#include <zxtest/zxtest.h>

namespace acpidump {
namespace {

// Wrapper around "ParseArgs" to simplify calling from tests.
bool ParseArgs(std::initializer_list<const char*> args, Args* result) {
  return ParseArgs(fbl::Span<const char* const>(args.begin(), args.size()), result);
}

TEST(ParseArgs, Table) {
  Args result;
  EXPECT_TRUE(ParseArgs({"acpidump", "-t", "table"}, &result));
  EXPECT_EQ(result.table, "table");
}

TEST(ParseArgs, MissingTable) {
  Args result;
  EXPECT_FALSE(ParseArgs({"acpidump", "-t"}, &result));
}

TEST(ParseArgs, Summary) {
  Args result;
  EXPECT_FALSE(result.table_names_only);
  EXPECT_TRUE(ParseArgs({"acpidump", "-s"}, &result));
  EXPECT_TRUE(result.table_names_only);
}

TEST(ParseArgs, InvalidArg) {
  Args result;
  EXPECT_FALSE(ParseArgs({"acpidump", "--invalid"}, &result));
}

TEST(ParseArgs, ExtraArg) {
  Args result;
  EXPECT_FALSE(ParseArgs({"acpidump", "-s", "extra"}, &result));
}

TEST(ParseArgs, Help) {
  {
    Args result;
    EXPECT_TRUE(ParseArgs({"acpidump", "--help"}, &result));
    EXPECT_TRUE(result.show_help);
  }
  {
    Args result;
    EXPECT_TRUE(ParseArgs({"acpidump", "-h"}, &result));
    EXPECT_TRUE(result.show_help);
  }
}

TEST(AcpiDump, Summary) {
  EXPECT_EQ(0, acpidump::Main(2, std::vector<const char*>({"acpidump", "-s"}).data()));
}

}  // namespace
}  // namespace acpidump

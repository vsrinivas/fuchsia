// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/command_utils.h"

#include <inttypes.h>

#include <limits>

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/expr/expr_parser.h"
#include "src/developer/debug/zxdb/symbols/base_type.h"
#include "src/developer/debug/zxdb/symbols/function.h"
#include "src/developer/debug/zxdb/symbols/location.h"
#include "src/developer/debug/zxdb/symbols/namespace.h"
#include "src/developer/debug/zxdb/symbols/process_symbols_test_setup.h"
#include "src/developer/debug/zxdb/symbols/type_test_support.h"
#include "src/developer/debug/zxdb/symbols/variable_test_support.h"
#include "src/lib/fxl/strings/string_printf.h"

namespace zxdb {

TEST(CommandUtils, StringToInt) {
  // Leading 0's not allowed.
  int result = 0;
  EXPECT_TRUE(StringToInt("010", &result).has_error());

  // Negative hexadecimal.
  EXPECT_FALSE(StringToInt("-0x1a", &result).has_error());
  EXPECT_EQ(-0x1a, result);

  // Trimmed
  EXPECT_FALSE(StringToInt("   -0x1a     ", &result).has_error());
  EXPECT_EQ(-0x1a, result);

  // Test at the limits.
  constexpr int kMax = std::numeric_limits<int>::max();
  EXPECT_FALSE(StringToInt(fxl::StringPrintf("%d", kMax), &result).has_error());
  EXPECT_EQ(kMax, result);

  constexpr int kMin = std::numeric_limits<int>::lowest();
  EXPECT_FALSE(StringToInt(fxl::StringPrintf("%d", kMin), &result).has_error());
  EXPECT_EQ(kMin, result);

  // Test just beyond the limits.
  int64_t kBeyondMax = static_cast<int64_t>(kMax) + 1;
  EXPECT_TRUE(StringToInt(fxl::StringPrintf("%" PRId64, kBeyondMax), &result)
                  .has_error());

  int64_t kBeyondMin = static_cast<int64_t>(kMin) - 1;
  EXPECT_TRUE(StringToInt(fxl::StringPrintf("%" PRId64, kBeyondMin), &result)
                  .has_error());
}

TEST(CommandUtils, StringToUint32) {
  uint32_t result = 0;
  EXPECT_TRUE(StringToUint32("032", &result).has_error());

  EXPECT_FALSE(StringToUint32("32", &result).has_error());
  EXPECT_EQ(32u, result);

  // Test at and just beyond the limits.
  EXPECT_FALSE(StringToUint32("0xffffffff", &result).has_error());
  EXPECT_EQ(0xffffffff, result);
  EXPECT_TRUE(StringToUint32("0x100000000", &result).has_error());

  // Trimming
  EXPECT_FALSE(StringToUint32("    0xffffffff     ", &result).has_error());
  EXPECT_EQ(0xffffffff, result);
}

TEST(CommandUtils, StringToUint64) {
  uint64_t result = 0;
  EXPECT_FALSE(StringToUint64("1234", &result).has_error());
  EXPECT_EQ(1234u, result);

  // Empty string.
  EXPECT_TRUE(StringToUint64("", &result).has_error());

  // Non-numbers.
  EXPECT_TRUE(StringToUint64("asdf", &result).has_error());
  EXPECT_TRUE(StringToUint64(" ", &result).has_error());

  // We don't allow "+" for positive numbers.
  EXPECT_TRUE(StringToUint64("+1234", &result).has_error());
  EXPECT_EQ(0u, result);

  // Trim.
  EXPECT_FALSE(StringToUint64("   1234   ", &result).has_error());
  EXPECT_EQ(1234u, result);

  // Leading 0's should trigger an error about dangerous octal usage.
  EXPECT_TRUE(StringToUint64("01234", &result).has_error());

  // Hex digits invalid without proper prefix.
  EXPECT_TRUE(StringToUint64("12a34", &result).has_error());

  // Valid hex number
  EXPECT_FALSE(StringToUint64("0x1A2a34", &result).has_error());
  EXPECT_EQ(0x1a2a34u, result);

  // Isolated hex prefix.
  EXPECT_TRUE(StringToUint64("0x", &result).has_error());

  // Valid hex number with capital X prefix at the max of a 64-bit int.
  EXPECT_FALSE(StringToUint64("0XffffFFFFffffFFFF", &result).has_error());
  EXPECT_EQ(0xffffFFFFffffFFFFu, result);
}

TEST(CommandUtils, ReadUint64Arg) {
  Command cmd;
  uint64_t out;

  Err err = ReadUint64Arg(cmd, 0, "code", &out);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Not enough arguments when reading the code.", err.msg());

  std::vector<std::string> args;
  args.push_back("12");
  args.push_back("0x67");
  args.push_back("notanumber");
  cmd.set_args(std::move(args));

  err = ReadUint64Arg(cmd, 0, "code", &out);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(12u, out);

  err = ReadUint64Arg(cmd, 1, "code", &out);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(0x67u, out);

  err = ReadUint64Arg(cmd, 2, "code", &out);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Invalid number \"notanumber\" when reading the code.", err.msg());
}

TEST(CommandUtils, ParseHostPort) {
  std::string host;
  uint16_t port;

  // Host good.
  EXPECT_FALSE(ParseHostPort("google.com:1234", &host, &port).has_error());
  EXPECT_EQ("google.com", host);
  EXPECT_EQ(1234, port);

  EXPECT_FALSE(ParseHostPort("google.com", "1234", &host, &port).has_error());
  EXPECT_EQ("google.com", host);
  EXPECT_EQ(1234, port);

  // IPv4 Good.
  EXPECT_FALSE(ParseHostPort("192.168.0.1:1234", &host, &port).has_error());
  EXPECT_EQ("192.168.0.1", host);
  EXPECT_EQ(1234, port);

  EXPECT_FALSE(ParseHostPort("192.168.0.1", "1234", &host, &port).has_error());
  EXPECT_EQ("192.168.0.1", host);
  EXPECT_EQ(1234, port);

  // IPv6 Good.
  EXPECT_FALSE(ParseHostPort("[1234::5678]:1234", &host, &port).has_error());
  EXPECT_EQ("1234::5678", host);
  EXPECT_EQ(1234, port);

  EXPECT_FALSE(ParseHostPort("[1234::5678]", "1234", &host, &port).has_error());
  EXPECT_EQ("1234::5678", host);
  EXPECT_EQ(1234, port);

  EXPECT_FALSE(ParseHostPort("1234::5678", "1234", &host, &port).has_error());
  EXPECT_EQ("1234::5678", host);
  EXPECT_EQ(1234, port);

  // Missing ports.
  EXPECT_TRUE(ParseHostPort("google.com", &host, &port).has_error());
  EXPECT_TRUE(ParseHostPort("192.168.0.1", &host, &port).has_error());
  EXPECT_TRUE(ParseHostPort("1234::5678", &host, &port).has_error());
  EXPECT_TRUE(ParseHostPort("[1234::5678]", &host, &port).has_error());

  // Bad port values.
  EXPECT_TRUE(ParseHostPort("google.com:0", &host, &port).has_error());
  EXPECT_TRUE(ParseHostPort("google.com:99999999", &host, &port).has_error());
}

TEST(CommandUtils, FormatIdentifier) {
  // Regular name.
  OutputBuffer output = FormatIdentifier(Identifier("ThisIsAName"), false);
  EXPECT_EQ("kNormal \"ThisIsAName\"", output.GetDebugString());

  // Regular name with bolding.
  output = FormatIdentifier(Identifier("ThisIsAName"), true);
  EXPECT_EQ("kHeading \"ThisIsAName\"", output.GetDebugString());

  // Hierarchical name.
  ParsedIdentifier ident;
  Err err = ExprParser::ParseIdentifier("::Foo<int, char*>::Bar<Baz>", &ident);
  ASSERT_FALSE(err.has_error());
  EXPECT_EQ(
      "kNormal \"::Foo\", "
      "kComment \"<int, char*>\", "
      "kNormal \"::\", "
      "kHeading \"Bar\", "
      "kComment \"<Baz>\"",
      FormatIdentifier(ident, true).GetDebugString());
}

TEST(CommandUtils, FormatFunctionName) {
  auto function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_assigned_name("Function");

  // Function with no parameters.
  EXPECT_EQ("Function()", FormatFunctionName(function.get(), false).AsString());
  EXPECT_EQ("Function()", FormatFunctionName(function.get(), true).AsString());

  // Add two parameters.
  auto int32_type = MakeInt32Type();
  auto param_value = MakeVariableForTest("value", int32_type, 0x100, 0x200,
                                         std::vector<uint8_t>());
  auto param_other = MakeVariableForTest("other_param", int32_type, 0x100,
                                         0x200, std::vector<uint8_t>());
  function->set_parameters({LazySymbol(param_value), LazySymbol(param_other)});

  EXPECT_EQ("Function(…)",
            FormatFunctionName(function.get(), false).AsString());
  EXPECT_EQ("Function(int32_t, int32_t)",
            FormatFunctionName(function.get(), true).AsString());

  // Put in a namespace and add some templates. This needs a new function
  // because the name will be cached above.
  function = fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram);
  function->set_assigned_name("Function<int>");
  function->set_parameters({LazySymbol(param_value), LazySymbol(param_other)});

  auto ns = fxl::MakeRefCounted<Namespace>();
  ns->set_assigned_name("ns");
  function->set_parent(LazySymbol(ns));

  EXPECT_EQ(
      "kNormal \"ns::\", "
      "kHeading \"Function\", "
      "kComment \"<int>(…)\"",
      FormatFunctionName(function.get(), false).GetDebugString());

  function->set_parent(LazySymbol());
}

TEST(CommandUtils, FormatLocation) {
  SymbolContext symbol_context = SymbolContext::ForRelativeAddresses();

  EXPECT_EQ("<invalid address>",
            FormatLocation(nullptr, Location(), true, false).AsString());

  // Address-only location.
  EXPECT_EQ(
      "0x12345",
      FormatLocation(nullptr, Location(Location::State::kAddress, 0x12345),
                     false, false)
          .AsString());

  // Function-only location.
  fxl::RefPtr<Function> function(
      fxl::MakeRefCounted<Function>(DwarfTag::kSubprogram));
  function->set_assigned_name("Func");
  function->set_code_ranges(AddressRanges(AddressRange(0x1200, 0x1300)));
  EXPECT_EQ("Func() + 0x34 (no line info)",
            FormatLocation(nullptr,
                           Location(0x1234, FileLine(), 0, symbol_context,
                                    LazySymbol(function)),
                           false, false)
                .AsString());

  // Same as above but location is before the function address (probably
  // something is corrupt). It should omit the offset.
  EXPECT_EQ("Func()",
            FormatLocation(nullptr,
                           Location(0x1100, FileLine(), 0, symbol_context,
                                    LazySymbol(function)),
                           false, false)
                .AsString());

  // File/line-only location.
  EXPECT_EQ("/path/foo.cc:21",
            FormatLocation(nullptr,
                           Location(0x1234, FileLine("/path/foo.cc", 21), 0,
                                    symbol_context),
                           false, false)
                .AsString());

  // Full location.
  Location loc(0x1234, FileLine("/path/foo.cc", 21), 0, symbol_context,
               LazySymbol(function));
  EXPECT_EQ("0x1234, Func() • /path/foo.cc:21",
            FormatLocation(nullptr, loc, true, false).AsString());
  EXPECT_EQ("Func() • /path/foo.cc:21",
            FormatLocation(nullptr, loc, false, false).AsString());
}

TEST(CommandUtils, DescribeFileLine) {
  FileLine fl("/path/to/foo.cc", 21);
  EXPECT_EQ("/path/to/foo.cc:21", DescribeFileLine(nullptr, fl));

  // Missing line number.
  EXPECT_EQ("/path/foo.cc:?",
            DescribeFileLine(nullptr, FileLine("/path/foo.cc", 0)));

  // Missing both.
  EXPECT_EQ("?:?", DescribeFileLine(nullptr, FileLine()));

  // Pass an TargetSymbols to trigger path shortening. Since the TargetSymbols
  // has no files to match, the name will be unique and we'll get just the name
  // part.
  ProcessSymbolsTestSetup setup;
  EXPECT_EQ("foo.cc:21", DescribeFileLine(&setup.target(), fl));
}

TEST(SetElementsToAdd, TestCase) {
  Err err;
  AssignType assign_type;
  std::vector<std::string> out;

  err = SetElementsToAdd({""}, &assign_type, &out);
  EXPECT_TRUE(err.has_error());

  err = SetElementsToAdd({"option_name"}, &assign_type, &out);
  EXPECT_TRUE(err.has_error());

  // Assign.

  err = SetElementsToAdd({"option_name", "value"}, &assign_type, &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(assign_type, AssignType::kAssign);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0], "value");

  // Equal assign.

  err = SetElementsToAdd({"option_name", "="}, &assign_type, &out);
  EXPECT_TRUE(err.has_error());

  err = SetElementsToAdd({"option_name", "=", "value"}, &assign_type, &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(assign_type, AssignType::kAssign);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0], "value");

  // Multiple assign.

  err =
      SetElementsToAdd({"option_name", "value", "value2"}, &assign_type, &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(assign_type, AssignType::kAssign);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0], "value");
  EXPECT_EQ(out[1], "value2");

  err = SetElementsToAdd({"option_name", "=", "value", "value2"}, &assign_type,
                         &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(assign_type, AssignType::kAssign);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0], "value");
  EXPECT_EQ(out[1], "value2");

  // Append.

  err = SetElementsToAdd({"option_name", "+="}, &assign_type, &out);
  EXPECT_TRUE(err.has_error());

  err = SetElementsToAdd({"option_name", "+=", "value"}, &assign_type, &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(assign_type, AssignType::kAppend);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0], "value");

  err = SetElementsToAdd({"option_name", "+=", "value", "value2"}, &assign_type,
                         &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(assign_type, AssignType::kAppend);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0], "value");
  EXPECT_EQ(out[1], "value2");

  // Remove.

  err = SetElementsToAdd({"option_name", "-="}, &assign_type, &out);
  EXPECT_TRUE(err.has_error());

  err = SetElementsToAdd({"option_name", "-=", "value"}, &assign_type, &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(assign_type, AssignType::kRemove);
  ASSERT_EQ(out.size(), 1u);
  EXPECT_EQ(out[0], "value");

  err = SetElementsToAdd({"option_name", "-=", "value", "value2"}, &assign_type,
                         &out);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(assign_type, AssignType::kRemove);
  ASSERT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0], "value");
  EXPECT_EQ(out[1], "value2");
}

}  // namespace zxdb

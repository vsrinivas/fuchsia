// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/console/command_utils.h"

#include <inttypes.h>

#include <limits>

#include "gtest/gtest.h"
#include "src/developer/debug/zxdb/client/filter.h"
#include "src/developer/debug/zxdb/client/mock_breakpoint.h"
#include "src/developer/debug/zxdb/client/mock_breakpoint_location.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/command.h"
#include "src/developer/debug/zxdb/console/console_context.h"
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
  EXPECT_TRUE(StringToInt(fxl::StringPrintf("%" PRId64, kBeyondMax), &result).has_error());

  int64_t kBeyondMin = static_cast<int64_t>(kMin) - 1;
  EXPECT_TRUE(StringToInt(fxl::StringPrintf("%" PRId64, kBeyondMin), &result).has_error());
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

TEST(CommandUtils, FormatInputLocation) {
  EXPECT_EQ("<no location>", FormatInputLocation(InputLocation()).AsString());
  EXPECT_EQ("0x123456", FormatInputLocation(InputLocation(0x123456)).AsString());
  EXPECT_EQ("file.cc:34", FormatInputLocation(InputLocation(FileLine("file.cc", 34))).AsString());
  EXPECT_EQ("Foo",
            FormatInputLocation(InputLocation(Identifier(IdentifierComponent("Foo")))).AsString());
}

TEST(CommandUtils, FormatInputLocations) {
  std::vector<InputLocation> input;
  EXPECT_EQ("<no location>", FormatInputLocations(input).AsString());

  input.emplace_back(Identifier(IdentifierComponent("Foo")));
  EXPECT_EQ("Foo", FormatInputLocations(input).AsString());

  input.emplace_back(Identifier(IdentifierComponent("Bar")));
  EXPECT_EQ("Foo, Bar", FormatInputLocations(input).AsString());
}

TEST(CommandUtils, FormatBreakpoint) {
  Session session;
  ConsoleContext context(&session);
  MockBreakpoint breakpoint(&session);

  // Register with the context.
  SystemObserver* system_observer = &context;
  system_observer->DidCreateBreakpoint(&breakpoint);

  // Formatting an empty breakpoint
  EXPECT_EQ("Breakpoint 1 pending @ <no location>\n",
            FormatBreakpoint(&context, &breakpoint, false).AsString());

  // Should show no message for context.
  EXPECT_EQ("Breakpoint 1 pending @ <no location>\n",
            FormatBreakpoint(&context, &breakpoint, true).AsString());

  // Provide settings.
  BreakpointSettings settings;
  settings.locations.push_back(InputLocation(FileLine("foo.cc", 21)));
  breakpoint.SetSettings(settings);

  // Format pending.
  EXPECT_EQ("Breakpoint 1 pending @ foo.cc:21\n",
            FormatBreakpoint(&context, &breakpoint, false).AsString());
  EXPECT_EQ(
      "Breakpoint 1 @ foo.cc:21\n"
      "Pending: No current matches for location. It will be matched against new\n"
      "         processes and shared libraries.\n",
      FormatBreakpoint(&context, &breakpoint, true).AsString());

  // Provide matched location. The formatting doesn't use the Process so we provide a null one.
  std::vector<std::unique_ptr<BreakpointLocation>> locs;
  locs.push_back(std::make_unique<MockBreakpointLocation>(nullptr));
  breakpoint.set_locations(std::move(locs));

  // No source context.
  EXPECT_EQ("Breakpoint 1 @ foo.cc:21\n",
            FormatBreakpoint(&context, &breakpoint, false).AsString());

  // Provide 2 matched locations
  locs.push_back(std::make_unique<MockBreakpointLocation>(nullptr));
  locs.push_back(std::make_unique<MockBreakpointLocation>(nullptr));
  breakpoint.set_locations(std::move(locs));

  EXPECT_EQ("Breakpoint 1 (2 addrs) @ foo.cc:21\n",
            FormatBreakpoint(&context, &breakpoint, false).AsString());

  // Set all the options to non-defaults.
  settings.type = BreakpointSettings::Type::kWrite;
  settings.byte_size = 4;
  settings.enabled = false;
  settings.scope = ExecutionScope(session.system().GetTargets()[0]);
  settings.stop_mode = BreakpointSettings::StopMode::kThread;
  settings.one_shot = true;
  breakpoint.SetSettings(settings);
  EXPECT_EQ(
      "Breakpoint 1 scope=\"pr 1\" stop=thread enabled=false type=write size=4 one-shot=true (2 "
      "addrs) @ foo.cc:21\n",
      FormatBreakpoint(&context, &breakpoint, false).AsString());

  system_observer->WillDestroyBreakpoint(&breakpoint);
}

TEST(CommandUtils, FormatFilter) {
  Session session;
  ConsoleContext context(&session);

  Filter* f = session.system().CreateNewFilter();
  EXPECT_EQ("Filter 1 \"\" (disabled) for all jobs.", FormatFilter(&context, f).AsString());

  f->SetPattern(Filter::kAllProcessesPattern);
  EXPECT_EQ("Filter 1 \"*\" (all processes) for all jobs.", FormatFilter(&context, f).AsString());

  // This will be job 2 since the system should have a default job.
  JobContext* job = session.system().CreateNewJobContext();
  f->SetJob(job);
  f->SetPattern("foo");
  EXPECT_EQ("Filter 1 \"foo\" for job 2.", FormatFilter(&context, f).AsString());
}

TEST(CommandUtils, FormatConsoleString) {
  EXPECT_EQ("\"\"", FormatConsoleString(""));

  // Doesn't need any escaping or quoting.
  EXPECT_EQ("foobar", FormatConsoleString("foobar"));
  EXPECT_EQ("snowman\xe2\x98\x83!", FormatConsoleString("snowman\xe2\x98\x83!"));

  // Needs normal quoting.
  EXPECT_EQ("\"foo\\nbar\"", FormatConsoleString("foo\nbar"));
  EXPECT_EQ("\"foo bar\"", FormatConsoleString("foo bar"));
  EXPECT_EQ("\"f\\x01oo\"", FormatConsoleString("f\x01oo"));

  // Can use raw quoting. Test making the delimiters unique.
  EXPECT_EQ("R\"(foo \"bar\")\"", FormatConsoleString("foo \"bar\""));
  EXPECT_EQ("R\"*(raw end )\")*\"", FormatConsoleString("raw end )\""));
  EXPECT_EQ("R\"**(raw end )\" )*\")**\"", FormatConsoleString("raw end )\" )*\""));
}

}  // namespace zxdb

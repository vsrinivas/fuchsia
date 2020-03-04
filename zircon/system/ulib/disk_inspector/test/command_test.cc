// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "disk_inspector/command.h"

#include <zxtest/zxtest.h>

namespace disk_inspector {
namespace {

TEST(CommandTest, PrintCommand) {
  Command command = {"command",
                     {
                         {"field1", ArgType::kString, "test1"},
                         {"field2", ArgType::kString, "test2"},
                         {"field3", ArgType::kString, "test3"},
                     },
                     "test",
                     nullptr};
  EXPECT_STR_EQ(PrintCommand(command), "command [field1] [field2] [field3]");
}

TEST(CommandTest, PrintCommands) {
  std::vector<Command> commands = {
      {"command1",
       {
           {"field1", ArgType::kString, "test1"},
       },
       "test",
       nullptr},
      {"command2",
       {
           {"field1", ArgType::kString, "test1"},
           {"field2", ArgType::kString, "test2"},
       },
       "test",
       nullptr},
      {"command3",
       {
           {"field1", ArgType::kString, "test1"},
           {"field2", ArgType::kString, "test2"},
           {"field3", ArgType::kString, "test3"},
       },
       "test",
       nullptr},
  };

  std::string expected =
      R"""(command1 [field1]
	test
		field1: test1

command2 [field1] [field2]
	test
		field1: test1
		field2: test2

command3 [field1] [field2] [field3]
	test
		field1: test1
		field2: test2
		field3: test3

)""";

  EXPECT_STR_EQ(PrintCommandList(commands).c_str(), expected.c_str());
}

TEST(CommandTest, ParseCommand) {
  Command command = {"command",
                     {
                         {"field1", ArgType::kString, "test1"},
                         {"field2", ArgType::kUint64, "test2"},
                         {"field3", ArgType::kUint64, "test3"},
                         {"field4", ArgType::kString, "test4"},
                     },
                     "test",
                     nullptr};
  std::vector<std::string> input = {"command", "testing", "123", "42", "hello"};
  fit::result<ParsedCommand, zx_status_t> result = disk_inspector::ParseCommand(input, command);
  ASSERT_TRUE(result.is_ok());
  ParsedCommand parsed = result.take_ok_result().value;
  ASSERT_FALSE(parsed.string_fields.find("field1") == parsed.string_fields.end());
  ASSERT_FALSE(parsed.uint64_fields.find("field2") == parsed.uint64_fields.end());
  ASSERT_FALSE(parsed.uint64_fields.find("field3") == parsed.uint64_fields.end());
  ASSERT_FALSE(parsed.string_fields.find("field4") == parsed.string_fields.end());
  EXPECT_STR_EQ(parsed.string_fields["field1"], "testing");
  EXPECT_EQ(parsed.uint64_fields["field2"], 123);
  EXPECT_EQ(parsed.uint64_fields["field3"], 42);
  EXPECT_STR_EQ(parsed.string_fields["field4"], "hello");
}

TEST(CommandTest, ParseCommandInvalidArgumentNumberFail) {
  Command command = {"command",
                     {
                         {"field1", ArgType::kString, "test1"},
                     },
                     "test",
                     nullptr};
  std::vector<std::string> input = {"command", "testing", "123", "42", "hello"};
  fit::result<ParsedCommand, zx_status_t> result = disk_inspector::ParseCommand(input, command);
  ASSERT_TRUE(result.is_error());
}

TEST(CommandTest, ParseCommandInvalidTypeFail) {
  Command command = {"command",
                     {
                         {"field1", ArgType::kUint64, "test1"},
                     },
                     "test",
                     nullptr};
  std::vector<std::string> input = {"command", "testing"};
  fit::result<ParsedCommand, zx_status_t> result = disk_inspector::ParseCommand(input, command);
  ASSERT_TRUE(result.is_error());
}

}  // namespace
}  // namespace disk_inspector

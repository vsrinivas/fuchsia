// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/command_parser.h"

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/console/command.h"
#include "gtest/gtest.h"

namespace zxdb {

TEST(CommandParser, Tokenizer) {
  std::vector<std::string> output;

  EXPECT_FALSE(TokenizeCommand("", &output).has_error());
  EXPECT_TRUE(output.empty());

  EXPECT_FALSE(TokenizeCommand("   ", &output).has_error());
  EXPECT_TRUE(output.empty());

  EXPECT_FALSE(TokenizeCommand("a", &output).has_error());
  EXPECT_EQ(1u, output.size());
  EXPECT_EQ("a", output[0]);

  EXPECT_FALSE(TokenizeCommand("ab cd", &output).has_error());
  EXPECT_EQ(2u, output.size());
  EXPECT_EQ("ab", output[0]);
  EXPECT_EQ("cd", output[1]);

  EXPECT_FALSE(TokenizeCommand("  ab  cd  ", &output).has_error());
  EXPECT_EQ(2u, output.size());
  EXPECT_EQ("ab", output[0]);
  EXPECT_EQ("cd", output[1]);
}

TEST(CommandParser, ParserBasic) {
  Command output;

  EXPECT_FALSE(ParseCommand("", &output).has_error());
  EXPECT_EQ(Noun::kNone, output.noun);
  EXPECT_EQ(Verb::kNone, output.verb);
  EXPECT_TRUE(output.args.empty());

  EXPECT_FALSE(ParseCommand("   ", &output).has_error());
  EXPECT_EQ(Noun::kNone, output.noun);
  EXPECT_EQ(Verb::kNone, output.verb);
  EXPECT_TRUE(output.args.empty());

  // Noun-only.
  EXPECT_FALSE(ParseCommand("process", &output).has_error());
  EXPECT_EQ(Noun::kProcess, output.noun);
  EXPECT_EQ(Verb::kNone, output.verb);
  EXPECT_TRUE(output.args.empty());

  // Noun-verb shortcut.
  EXPECT_FALSE(ParseCommand("r", &output).has_error());
  EXPECT_EQ(Noun::kProcess, output.noun);
  EXPECT_EQ(Verb::kRun, output.verb);
  EXPECT_TRUE(output.args.empty());

  // Noun-verb shortcut with args.
  EXPECT_FALSE(ParseCommand(" r foo bar ", &output).has_error());
  EXPECT_EQ(Noun::kProcess, output.noun);
  EXPECT_EQ(Verb::kRun, output.verb);
  ASSERT_EQ(2u, output.args.size());
  EXPECT_EQ("foo", output.args[0]);
  EXPECT_EQ("bar", output.args[1]);

  // Noun-only shortcut.
  EXPECT_FALSE(ParseCommand("pro run", &output).has_error());
  EXPECT_EQ(Noun::kProcess, output.noun);
  EXPECT_EQ(Verb::kRun, output.verb);
  EXPECT_TRUE(output.args.empty());

  // Noun-only shortcut with args.
  EXPECT_FALSE(ParseCommand("pro run foo bar", &output).has_error());
  EXPECT_EQ(Noun::kProcess, output.noun);
  EXPECT_EQ(Verb::kRun, output.verb);
  ASSERT_EQ(2u, output.args.size());
  EXPECT_EQ("foo", output.args[0]);
  EXPECT_EQ("bar", output.args[1]);
}

TEST(CommandParser, ParserBasicErrors) {
  Command output;

  // Unknown command.
  Err err = ParseCommand("zzyzx", &output);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Unknown command \"zzyzx\". See \"help\".", err.msg());

  // Unknown verb.
  err = ParseCommand("pro zzyzx", &output);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Unknown verb. See \"help process\".", err.msg());

  // Mismatched noun/verb.
  err = ParseCommand("pro up", &output);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Invalid combination \"process up\". See \"help process\".",
            err.msg());
}

TEST(CommandParser, Switches) {
  Command output;

  // Look up the command ID for the size switch to "memory read". This allows
  // the checks below
  // to stay in sync without knowing much about how the memory command is
  // implemented.
  output.noun = Noun::kMemory;
  output.verb = Verb::kRead;
  const CommandRecord& read_record = GetRecordForCommand(output);
  ASSERT_TRUE(read_record.exec);
  const SwitchRecord* size_switch = nullptr;
  for (const auto& sr : read_record.switches) {
    if (sr.ch == 's') {
      size_switch = &sr;
      break;
    }
  }
  ASSERT_TRUE(size_switch);

  Err err = ParseCommand("memory read -", &output);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Invalid switch \"-\".", err.msg());

  // Valid long switch.
  err = ParseCommand("memory read --size 234 next", &output);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(1u, output.switches.size());
  EXPECT_EQ("234", output.switches[size_switch->id]);
  ASSERT_EQ(1u, output.args.size());
  EXPECT_EQ("next", output.args[0]);

  // Expects a value for a long switch.
  err = ParseCommand("memory read --size", &output);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Parameter needed for \"--size\".", err.msg());

  // Valid short switch with value following.
  err = ParseCommand("memory read -s 567 next", &output);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(1u, output.switches.size());
  EXPECT_EQ("567", output.switches[size_switch->id]);
  ASSERT_EQ(1u, output.args.size());
  EXPECT_EQ("next", output.args[0]);

  // Valid short switch with value concatenated.
  err = ParseCommand("memory read -s567 next", &output);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(1u, output.switches.size());
  EXPECT_EQ("567", output.switches[size_switch->id]);
  ASSERT_EQ(1u, output.args.size());
  EXPECT_EQ("next", output.args[0]);

  // Expects a value for a short switch.
  err = ParseCommand("memory read -s", &output);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Parameter needed for \"-s\".", err.msg());
}

}  // namespace zxdb

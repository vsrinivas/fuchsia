// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/command_parser.h"

#include "garnet/bin/zxdb/common/err.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/nouns.h"
#include "gtest/gtest.h"

namespace zxdb {

namespace {

bool CompletionContains(const std::vector<std::string>& suggestions,
                        const std::string& contains) {
  return std::find(suggestions.begin(), suggestions.end(), contains) !=
         suggestions.end();
}

}  // namespace

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

  // Verb-only command.
  Err err = ParseCommand("run", &output);
  EXPECT_FALSE(err.has_error());
  EXPECT_TRUE(output.nouns().empty());
  EXPECT_EQ(Verb::kRun, output.verb());

  // Noun-only command.
  err = ParseCommand("process", &output);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(1u, output.nouns().size());
  EXPECT_TRUE(output.HasNoun(Noun::kProcess));
  EXPECT_EQ(Command::kNoIndex, output.GetNounIndex(Noun::kProcess));
  EXPECT_EQ(Verb::kNone, output.verb());

  // Noun-index command.
  err = ParseCommand("process 1", &output);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(1u, output.nouns().size());
  EXPECT_TRUE(output.HasNoun(Noun::kProcess));
  EXPECT_EQ(1, output.GetNounIndex(Noun::kProcess));
  EXPECT_EQ(Verb::kNone, output.verb());

  // Noun-verb command.
  err = ParseCommand("process run", &output);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(1u, output.nouns().size());
  EXPECT_TRUE(output.HasNoun(Noun::kProcess));
  EXPECT_EQ(Command::kNoIndex, output.GetNounIndex(Noun::kProcess));
  EXPECT_EQ(Verb::kRun, output.verb());

  // Noun-index-verb command.
  err = ParseCommand("process 2 run", &output);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(1u, output.nouns().size());
  EXPECT_TRUE(output.HasNoun(Noun::kProcess));
  EXPECT_EQ(2, output.GetNounIndex(Noun::kProcess));
  EXPECT_EQ(Verb::kRun, output.verb());

  err = ParseCommand("process 2 thread 1 run", &output);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(2u, output.nouns().size());
  EXPECT_TRUE(output.HasNoun(Noun::kProcess));
  EXPECT_EQ(2, output.GetNounIndex(Noun::kProcess));
  EXPECT_TRUE(output.HasNoun(Noun::kThread));
  EXPECT_EQ(1, output.GetNounIndex(Noun::kThread));
  EXPECT_EQ(Verb::kRun, output.verb());
}

TEST(CommandParser, ParserBasicErrors) {
  Command output;

  // Unknown command in different contexts.
  Err err = ParseCommand("zzyzx", &output);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("The string \"zzyzx\" is not a valid verb.", err.msg());

  err = ParseCommand("process 1 zzyzx", &output);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("The string \"zzyzx\" is not a valid verb.", err.msg());

  err = ParseCommand("process 1 process run", &output);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Noun \"process\" specified twice.", err.msg());
}

TEST(CommandParser, NounSwitches) {
  Command output;

  // Look up the switch ID for the "-v" noun switch.
  const SwitchRecord* verbose_switch = nullptr;
  for (const auto& sr : GetNounSwitches()) {
    if (sr.ch == 'v') {
      verbose_switch = &sr;
      break;
    }
  }
  ASSERT_TRUE(verbose_switch);

  Err err = ParseCommand("frame -", &output);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Invalid switch \"-\".", err.msg());

  // Valid short switch.
  err = ParseCommand("frame -v", &output);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(1u, output.switches().size());
  EXPECT_TRUE(output.HasSwitch(verbose_switch->id));

  // Valid long switch.
  err = ParseCommand("frame --verbose", &output);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(1u, output.switches().size());
  EXPECT_TRUE(output.HasSwitch(verbose_switch->id));
}

TEST(CommandParser, VerbSwitches) {
  Command output;

  // Look up the switch ID for the size switch to "memory read". This allows
  // the checks below to stay in sync without knowing much about how the memory
  // command is implemented.
  const auto& verbs = GetVerbs();
  auto read_record = verbs.find(Verb::kMemRead);
  ASSERT_NE(read_record, verbs.end());
  const SwitchRecord* size_switch = nullptr;
  for (const auto& sr : read_record->second.switches) {
    if (sr.ch == 's') {
      size_switch = &sr;
      break;
    }
  }
  ASSERT_TRUE(size_switch);

  Err err = ParseCommand("mem-read -", &output);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Invalid switch \"-\".", err.msg());

  // Valid long switch with no equals.
  err = ParseCommand("mem-read --size 234 next", &output);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(1u, output.switches().size());
  EXPECT_EQ("234", output.GetSwitchValue(size_switch->id));
  ASSERT_EQ(1u, output.args().size());
  EXPECT_EQ("next", output.args()[0]);

  // Valid long switch with equals sign.
  err = ParseCommand("mem-read --size=234 next", &output);
  EXPECT_FALSE(err.has_error()) << err.msg();
  EXPECT_EQ(1u, output.switches().size());
  EXPECT_EQ("234", output.GetSwitchValue(size_switch->id));
  ASSERT_EQ(1u, output.args().size());
  EXPECT_EQ("next", output.args()[0]);

  // Valid long switch with equals and no value (this is OK, value is empty
  // string).
  err = ParseCommand("mem-read --size= next", &output);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(1u, output.switches().size());
  EXPECT_EQ("", output.GetSwitchValue(size_switch->id));
  ASSERT_EQ(1u, output.args().size());
  EXPECT_EQ("next", output.args()[0]);

  // Expects a value for a long switch.
  err = ParseCommand("mem-read --size", &output);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Parameter needed for \"--size\".", err.msg());

  // Valid short switch with value following.
  err = ParseCommand("mem-read -s 567 next", &output);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(1u, output.switches().size());
  EXPECT_EQ("567", output.GetSwitchValue(size_switch->id));
  ASSERT_EQ(1u, output.args().size());
  EXPECT_EQ("next", output.args()[0]);

  // Valid short switch with value concatenated.
  err = ParseCommand("mem-read -s567 next", &output);
  EXPECT_FALSE(err.has_error());
  EXPECT_EQ(1u, output.switches().size());
  EXPECT_EQ("567", output.GetSwitchValue(size_switch->id));
  ASSERT_EQ(1u, output.args().size());
  EXPECT_EQ("next", output.args()[0]);

  // Expects a value for a short switch.
  err = ParseCommand("mem-read -s", &output);
  EXPECT_TRUE(err.has_error());
  EXPECT_EQ("Parameter needed for \"-s\".", err.msg());
}

TEST(CommandParser, Completions) {
  std::vector<std::string> comp;

  // Noun completion.
  comp = GetCommandCompletions("t");
  EXPECT_TRUE(CompletionContains(comp, "t"));
  EXPECT_TRUE(CompletionContains(comp, "thread"));

  // Verb completion.
  comp = GetCommandCompletions("h");
  EXPECT_TRUE(CompletionContains(comp, "h"));
  EXPECT_TRUE(CompletionContains(comp, "help"));

  // Noun + Verb completion.
  comp = GetCommandCompletions("process 2 p");
  EXPECT_TRUE(CompletionContains(comp, "process 2 pause"));

  // Ending in a space gives everything.
  comp = GetCommandCompletions("process ");
  EXPECT_TRUE(CompletionContains(comp, "process quit"));
  EXPECT_TRUE(CompletionContains(comp, "process run"));

  // No input should give everything
  comp = GetCommandCompletions("");
  EXPECT_TRUE(CompletionContains(comp, "run"));
  EXPECT_TRUE(CompletionContains(comp, "quit"));
}

}  // namespace zxdb

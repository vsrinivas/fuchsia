// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_parser.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/output_buffer.h"

namespace zxdb {

namespace {

const char kHelpHelp[] =
    R"(help

    Helps!)";

const char kQuickReference[] =
    R"(Common commands:
    run / r: Run the program.
    next / n: Step to next line.
    step / s: Step into.
    quit / q: Quit the debugger.)";

std::string GetNounReference() {
  std::string help = "Topics (type \"help <topic>\" for more):\n";
  const auto& nouns = GetNouns();
  for (const auto& pair : nouns) {
    help += "    " + std::string(NounToString(pair.first)) + "\n";
  }
  return help;
}

Err DoZxdbHelp(Session* session, const Command& cmd) {
  OutputBuffer out;

  if (cmd.args.empty()) {
    // Generic help, list topics and quick reference.
    out.FormatHelp(GetNounReference() + "\n" + kQuickReference);
    Console::get()->Output(std::move(out));
    return Err();
  }

  Command help_on;
  Err err = ParseCommand(cmd.args, &help_on);
  if (err.has_error()) {
    // Command not valid.
    out.OutputErr(err);
    out.FormatHelp(GetNounReference());
    Console::get()->Output(std::move(out));
    return Err();
  }

  const CommandRecord& record = GetRecordForCommand(help_on);
  std::string help;
  if (record.help)
    help = record.help;

  // When supplied with no verb, additionally lists verbs for the noun.
  if (help_on.verb == Verb::kNone) {
    const auto& verbs = GetVerbsForNoun(help_on.noun);
    help += "\nAvailable verbs for \"" +
            std::string(NounToString(help_on.noun)) + " <verb>\":\n";
    for (const auto& pair : verbs) {
      if (pair.first != Verb::kNone) {
        help += "    " + std::string(VerbToString(pair.first)) + "\n";
      }
    }
  }

  out.FormatHelp(help);
  Console::get()->Output(std::move(out));
  return Err();
}

const char kQuitHelp[] =
    R"(quit

    Quits the debugger.)";

Err DoZxdbQuit(Session* session, const Command& cmd) {
  // This command is special-cased by the main loop so it shouldn't get
  // executed.
  return Err();
}

}  // namespace

std::map<Verb, CommandRecord> GetZxdbVerbs() {
  std::map<Verb, CommandRecord> map;
  map[Verb::kHelp] = CommandRecord(&DoZxdbHelp, kHelpHelp);
  map[Verb::kQuit] = CommandRecord(&DoZxdbQuit, kQuitHelp);
  return map;
}

}  // namespace zxdb

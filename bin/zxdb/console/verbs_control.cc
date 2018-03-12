// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include <algorithm>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/output_buffer.h"

namespace zxdb {

namespace {

const char kHelpShortHelp[] =
    R"(help)";
const char kHelpHelp[] =
    R"(help

    Yo dawg, I heard you like help on your help so I put help on the help in
    the help.)";

const char kHelpIntro[] =
    R"(Help!

    Type "help <topic>" for more information.

Command syntax

    Verbs
        "step"
            Applies the "step" verb to the currently selected thread.
        "mem-read --size=16 0x12345678"
            Pass a named switch and an argument.

    Nouns
        "thread"
            List available threads
        "thread 1"
            Select thread with ID 1 to be the default.

    Verb-noun combinations
        "thread 4 step"
            Steps thread 4 of the current process regardless of the currently
            selected thread.
        "process 1 thread 4 step"
            Steps thread 4 of process 1 regardless of the currently selected
            thread or process.
)";

std::string GetReference() {
  std::string help = kHelpIntro;

  help += "\nNouns\n";
  std::vector<std::string> noun_lines;
  for (const auto& pair : GetNouns())
    noun_lines.push_back(pair.second.short_help);
  std::sort(noun_lines.begin(), noun_lines.end());
  for (const auto& line : noun_lines)
    help += "    " + line + "\n";

  help += "\nVerbs\n";
  std::vector<std::string> verb_lines;
  for (const auto& pair : GetVerbs())
    verb_lines.push_back(pair.second.short_help);
  std::sort(verb_lines.begin(), verb_lines.end());
  for (const auto& line : verb_lines)
    help += "    " + line + "\n";

  return help;
}

Err DoHelp(ConsoleContext* context, const Command& cmd) {
  OutputBuffer out;

  if (cmd.args().empty()) {
    // Generic help, list topics and quick reference.
    out.FormatHelp(GetReference() + "\n");
    Console::get()->Output(std::move(out));
    return Err();
  }
  const std::string& on_what = cmd.args()[0];

  const char* help = nullptr;

  // Check for a noun.
  const auto& string_noun = GetStringNounMap();
  auto found_string_noun = string_noun.find(on_what);
  if (found_string_noun != string_noun.end()) {
    // Find the noun record to get the help. This is guaranteed to exist.
    const auto& nouns = GetNouns();
    help = nouns.find(found_string_noun->second)->second.help;
  } else {
    // Check for a verb
    const auto& string_verb = GetStringVerbMap();
    auto found_string_verb = string_verb.find(on_what);
    if (found_string_verb != string_verb.end()) {
      // Find the verb record to get the help. This is guaranteed to exist.
      const auto& verbs = GetVerbs();
      help = verbs.find(found_string_verb->second)->second.help;
    } else {
      // Not a valid command.
      out.OutputErr(Err("\"" + on_what + "\" is not a valid command.\n"
                        "Try just \"help\" to get a list."));
      Console::get()->Output(std::move(out));
      return Err();
    }
  }

  out.FormatHelp(help);
  Console::get()->Output(std::move(out));
  return Err();
}

const char kQuitShortHelp[] =
    R"(quit: Quits the debugger.)";
const char kQuitHelp[] =
    R"(quit

    Quits the debugger.)";

Err DoQuit(ConsoleContext* context, const Command& cmd) {
  // This command is special-cased by the main loop so it shouldn't get
  // executed.
  return Err();
}

}  // namespace

void AppendControlVerbs(std::map<Verb, VerbRecord>* verbs) {
  (*verbs)[Verb::kHelp] =
      VerbRecord(&DoHelp, {"help", "h"}, kHelpShortHelp, kHelpHelp);
  (*verbs)[Verb::kQuit] =
      VerbRecord(&DoQuit, {"quit", "q"}, kQuitShortHelp, kQuitHelp);
}

}  // namespace zxdb

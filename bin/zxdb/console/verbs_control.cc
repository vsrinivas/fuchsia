// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include <algorithm>

#include "garnet/bin/zxdb/client/err.h"
#include "garnet/bin/zxdb/client/session.h"
#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/output_buffer.h"

namespace zxdb {

namespace {

// help ------------------------------------------------------------------------

const char kHelpShortHelp[] = R"(help / h: Help.)";
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

  Noun-Verb combinations
      "thread 4 step"
          Steps thread 4 of the current process regardless of the currently
          selected thread.
      "process 1 thread 4 step"
          Steps thread 4 of process 1 regardless of the currently selected
          thread or process.
)";

std::string FormatGroupHelp(const char* heading,
                            std::vector<std::string>* items) {
  std::sort(items->begin(), items->end());

  std::string help("\n");
  help.append(heading);
  help.append("\n");
  for (const auto& line : *items)
    help += "    " + line + "\n";
  return help;
}

std::string GetReference() {
  std::string help = kHelpIntro;

  // Group all verbs by their CommandGroup. Add nouns to this since people
  // will expect, for example, "breakpoint" to be in the breakpoints section.
  std::map<CommandGroup, std::vector<std::string>> groups;

  // Get the separate noun reference and add to the groups.
  help += "\nNouns\n";
  std::vector<std::string> noun_lines;
  for (const auto& pair : GetNouns()) {
    noun_lines.push_back(pair.second.short_help);
    groups[pair.second.command_group].push_back(pair.second.short_help);
  }
  std::sort(noun_lines.begin(), noun_lines.end());
  for (const auto& line : noun_lines)
    help += "    " + line + "\n";

  // Add in verbs.
  for (const auto& pair : GetVerbs())
    groups[pair.second.command_group].push_back(pair.second.short_help);

  help += FormatGroupHelp("General", &groups[CommandGroup::kGeneral]);
  help += FormatGroupHelp("Process", &groups[CommandGroup::kProcess]);
  help += FormatGroupHelp("Assembly", &groups[CommandGroup::kAssembly]);
  help += FormatGroupHelp("Breakpoint", &groups[CommandGroup::kBreakpoint]);
  help += FormatGroupHelp("Query", &groups[CommandGroup::kQuery]);
  help += FormatGroupHelp("Step", &groups[CommandGroup::kStep]);

  return help;
}

Err DoHelp(ConsoleContext* context, const Command& cmd) {
  OutputBuffer out;

  if (cmd.args().empty()) {
    // Generic help, list topics and quick reference.
    out.FormatHelp(GetReference());
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
      out.OutputErr(Err("\"" + on_what +
                        "\" is not a valid command.\n"
                        "Try just \"help\" to get a list."));
      Console::get()->Output(std::move(out));
      return Err();
    }
  }

  out.FormatHelp(help);
  Console::get()->Output(std::move(out));
  return Err();
}

// quit ------------------------------------------------------------------------

const char kQuitShortHelp[] = R"(quit / q: Quits the debugger.)";
const char kQuitHelp[] =
    R"(quit

  Quits the debugger.)";

Err DoQuit(ConsoleContext* context, const Command& cmd) {
  // This command is special-cased by the main loop so it shouldn't get
  // executed.
  return Err();
}

// connect ---------------------------------------------------------------------

const char kConnectShortHelp[] =
    R"(connect: Connect to a remote system for debugging.)";
const char kConnectHelp[] =
    R"(connect <remote_address>

  Connectes to a debug_agent at the given address/port. Both IP address and
  port are required.

  See also "disconnect".

Addresses

  Addresses can be of the form "<host> <port>" or "<host>:<port>". When using
  the latter form, IPv6 addresses must be [bracketed]. Otherwise the brackets
  are optional.

Examples

  connect mystem.localnetwork 1234
  connect mystem.localnetwork:1234
  connect 192.168.0.4:1234
  connect 192.168.0.4 1234
  connect [1234:5678::9abc] 1234
  connect 1234:5678::9abc 1234
  connect [1234:5678::9abc]:1234
)";

Err DoConnect(ConsoleContext* context, const Command& cmd) {
  // Can accept either one or two arg forms.
  std::string host;
  uint16_t port = 0;

  if (cmd.args().size() == 0) {
    return Err(ErrType::kInput, "Need host and port to connect to.");
  } else if (cmd.args().size() == 1) {
    Err err = ParseHostPort(cmd.args()[0], &host, &port);
    if (err.has_error())
      return err;
  } else if (cmd.args().size() == 2) {
    Err err = ParseHostPort(cmd.args()[0], cmd.args()[1], &host, &port);
    if (err.has_error())
      return err;
  } else {
    return Err(ErrType::kInput, "Too many arguments.");
  }

  context->session()->Connect(host, port, [](const Err& err) {
    if (err.has_error()) {
      // Don't display error message if they canceled the connection.
      if (err.type() != ErrType::kCanceled)
        Console::get()->Output(err);
    } else {
      OutputBuffer msg;
      msg.Append("Connected successfully.\n👉 ");
      msg.Append(Syntax::kComment,
                 "Normally you will \"run <program path>\" or \"attach "
                 "<process koid>\".");
      Console::get()->Output(std::move(msg));
    }
  });
  Console::get()->Output("Connecting (use \"disconnect\" to cancel)...\n");

  return Err();
}

// disconnect ------------------------------------------------------------------

const char kDisconnectShortHelp[] =
    R"(disconnect: Disconnect from the remote system.)";
const char kDisconnectHelp[] =
    R"(disconnect

  Disconnects from the remote system. There are no arguments.
)";

Err DoDisconnect(ConsoleContext* context, const Command& cmd) {
  if (!cmd.args().empty())
    return Err(ErrType::kInput, "\"disconnect\" takes no arguments.");

  context->session()->Disconnect([](const Err& err) {
    if (err.has_error())
      Console::get()->Output(err);
    else
      Console::get()->Output("Disconnected successfully.");
  });

  return Err();
}

}  // namespace

void AppendControlVerbs(std::map<Verb, VerbRecord>* verbs) {
  (*verbs)[Verb::kHelp] = VerbRecord(&DoHelp, {"help", "h"}, kHelpShortHelp,
                                     kHelpHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kQuit] = VerbRecord(&DoQuit, {"quit", "q"}, kQuitShortHelp,
                                     kQuitHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kConnect] =
      VerbRecord(&DoConnect, {"connect"}, kConnectShortHelp, kConnectHelp,
                 CommandGroup::kGeneral);
  (*verbs)[Verb::kDisconnect] =
      VerbRecord(&DoDisconnect, {"disconnect"}, kDisconnectShortHelp,
                 kDisconnectHelp, CommandGroup::kGeneral);
}

}  // namespace zxdb

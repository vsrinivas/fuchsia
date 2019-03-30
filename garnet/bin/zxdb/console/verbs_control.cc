// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/verbs.h"

#include <stdlib.h>

#include <algorithm>
#include <filesystem>

#include "garnet/bin/zxdb/console/command.h"
#include "garnet/bin/zxdb/console/command_utils.h"
#include "garnet/bin/zxdb/console/console.h"
#include "garnet/bin/zxdb/console/format_table.h"
#include "garnet/bin/zxdb/console/output_buffer.h"
#include "src/developer/debug/zxdb/client/session.h"
#include "src/developer/debug/zxdb/client/thread.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/common/string_util.h"

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
    Console::get()->Output(out);
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
      out.Append(Err("\"" + on_what +
                     "\" is not a valid command.\n"
                     "Try just \"help\" to get a list."));
      Console::get()->Output(out);
      return Err();
    }
  }

  out.FormatHelp(help);
  Console::get()->Output(out);
  return Err();
}

// quit ------------------------------------------------------------------------

const char kQuitShortHelp[] = R"(quit / q / exit: Quits the debugger.)";
const char kQuitHelp[] =
    R"(quit

  Quits the debugger.)";

Err DoQuit(ConsoleContext* context, const Command& cmd) {
  // This command is special-cased by the main loop so it shouldn't get
  // executed.
  return Err();
}

// quit-agent ------------------------------------------------------------------

const char kQuitAgentShortHelp[] = R"(quit-agent: Quits the debug agent.)";
const char kQuitAgentHelp[] =
    R"(quit-agent

  Quits the connected debug agent running on the target.)";

Err DoQuitAgent(ConsoleContext* context, const Command& cmd) {
  context->session()->QuitAgent([](const Err& err) {
    if (err.has_error()) {
      Console::get()->Output(err);
    } else {
      Console::get()->Output("Successfully stopped the debug agent.");
    }
  });

  return Err();
}

// connect ---------------------------------------------------------------------

const char kConnectShortHelp[] =
    R"(connect: Connect to a remote system for debugging.)";
const char kConnectHelp[] =
    R"(connect <remote_address>

  Connects to a debug_agent at the given address/port. Both IP address and port
  are required.

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

Err DoConnect(ConsoleContext* context, const Command& cmd,
              CommandCallback callback = nullptr) {
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

  context->session()->Connect(host, port, [callback, cmd](const Err& err) {
    if (err.has_error()) {
      // Don't display error message if they canceled the connection.
      if (err.type() != ErrType::kCanceled)
        Console::get()->Output(err);
    } else {
      OutputBuffer msg;
      msg.Append("Connected successfully.\n");

      // Assume if there's a callback this is not being run interactively.
      // Otherwise, show the usage tip.
      if (!callback) {
        msg.Append(Syntax::kWarning, "👉 ");
        msg.Append(Syntax::kComment,
                   "Normally you will \"run <program path>\" or \"attach "
                   "<process koid>\".");
      }
      Console::get()->Output(msg);
    }

    if (callback)
      callback(err);
  });
  Console::get()->Output("Connecting (use \"disconnect\" to cancel)...\n");

  return Err();
}

// opendump --------------------------------------------------------------------

const char kOpenDumpShortHelp[] =
    R"(opendump: Open a dump file for debugging.)";
const char kOpenDumpHelp[] =
    R"(opendump <path>

  Opens a minidump file. Currently only the 'minidump' format is supported.
)";

Err DoOpenDump(ConsoleContext* context, const Command& cmd,
               CommandCallback callback = nullptr) {
  std::string path;

  if (cmd.args().size() == 0) {
    return Err(ErrType::kInput, "Need path to open.");
  } else if (cmd.args().size() == 1) {
    path = cmd.args()[0];
  } else {
    return Err(ErrType::kInput, "Too many arguments.");
  }

  context->session()->OpenMinidump(path, [callback](const Err& err) {
    if (err.has_error()) {
      Console::get()->Output(err);
    } else {
      Console::get()->Output("Dump loaded successfully.\n");
    }

    if (callback)
      callback(err);
  });
  Console::get()->Output("Opening dump file...\n");

  return Err();
}

void DoCompleteOpenDump(const Command& cmd, const std::string& prefix,
                        std::vector<std::string>* completions) {
  if (!cmd.args().empty()) {
    return;
  }

  std::error_code ec;

  std::filesystem::path path;
  std::string filename;

  if (prefix.empty()) {
    path = std::filesystem::current_path(ec);

    if (ec) {
      return;
    }
  } else if (std::filesystem::exists(prefix, ec)) {
    if (!std::filesystem::is_directory(prefix, ec)) {
      completions->push_back(prefix);
      return;
    }

    path = std::filesystem::path(prefix) / "";
  } else {
    auto path_parts = std::filesystem::path(prefix);
    filename = path_parts.filename();

    if (filename.empty()) {
      return;
    }

    path = path_parts.parent_path();

    if (path.empty()) {
      path = std::filesystem::current_path(ec);

      if (ec) {
        return;
      }
    } else if (!std::filesystem::is_directory(path)) {
      return;
    }
  }

  for (const auto& item : std::filesystem::directory_iterator(path, ec)) {
    auto found = std::string(item.path().filename());

    if (!StringBeginsWith(found, filename)) {
      continue;
    }

    auto completion = prefix + found.substr(filename.size());

    if (item.is_directory(ec)) {
      completion += "/";
    }

    completions->push_back(completion);
  }
}

// disconnect ------------------------------------------------------------------

const char kDisconnectShortHelp[] =
    R"(disconnect: Disconnect from the remote system.)";
const char kDisconnectHelp[] =
    R"(disconnect

  Disconnects from the remote system, or cancels an in-progress connection if
  there is one.

  There are no arguments.
)";

Err DoDisconnect(ConsoleContext* context, const Command& cmd,
                 CommandCallback callback = nullptr) {
  if (!cmd.args().empty())
    return Err(ErrType::kInput, "\"disconnect\" takes no arguments.");

  context->session()->Disconnect([callback](const Err& err) {
    if (err.has_error())
      Console::get()->Output(err);
    else
      Console::get()->Output("Disconnected successfully.");

    // We call the given callback
    if (callback)
      callback(err);
  });

  return Err();
}

// cls -------------------------------------------------------------------------

const char kClsShortHelp[] = "cls: clear screen.";
const char kClsHelp[] =
    R"(cls

  Clears the contents of the console. Similar to "clear" on a shell.

  There are no arguments.
)";

Err DoCls(ConsoleContext* context, const Command& cmd,
          CommandCallback callback = nullptr) {
  if (!cmd.args().empty())
    return Err(ErrType::kInput, "\"cls\" takes no arguments.");

  Console::get()->Clear();

  if (callback)
    callback(Err());
  return Err();
}
}  // namespace

void AppendControlVerbs(std::map<Verb, VerbRecord>* verbs) {
  (*verbs)[Verb::kHelp] = VerbRecord(&DoHelp, {"help", "h"}, kHelpShortHelp,
                                     kHelpHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kQuit] =
      VerbRecord(&DoQuit, {"quit", "q", "exit"}, kQuitShortHelp, kQuitHelp,
                 CommandGroup::kGeneral);
  (*verbs)[Verb::kConnect] =
      VerbRecord(&DoConnect, {"connect"}, kConnectShortHelp, kConnectHelp,
                 CommandGroup::kGeneral);
  (*verbs)[Verb::kDisconnect] =
      VerbRecord(&DoDisconnect, {"disconnect"}, kDisconnectShortHelp,
                 kDisconnectHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kQuitAgent] =
      VerbRecord(&DoQuitAgent, {"quit-agent"}, kQuitAgentShortHelp,
                 kQuitAgentHelp, CommandGroup::kGeneral);
  (*verbs)[Verb::kOpenDump] = VerbRecord(
      &DoOpenDump, &DoCompleteOpenDump, {"opendump"}, kOpenDumpShortHelp,
      kOpenDumpHelp, CommandGroup::kGeneral, SourceAffinity::kNone);
  (*verbs)[Verb::kCls] = VerbRecord(&DoCls, {"cls"}, kClsShortHelp, kClsHelp,
                                    CommandGroup::kGeneral);
}

}  // namespace zxdb

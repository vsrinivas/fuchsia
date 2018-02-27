// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <string>
#include <vector>

namespace zxdb {

class Err;
class Session;

// Noun ------------------------------------------------------------------------

enum class Noun {
  kNone = 0,
  kBreakpoint,
  kFrame,
  kMemory,
  kProcess,
  kSystem,
  kThread,
  kZxdb,

  // Adding a new one?
  //   - Add in command_parser.cpp: GetShortcutMap for string -> noun
  //   conversion.
  //   - Add in NounToString for noun -> string conversion.
  kLast  // Not a real noun, keep last.
};

const char* NounToString(Noun n);

// Verb ------------------------------------------------------------------------

enum class Verb {
  kNone = 0,
  kAttach,
  kBacktrace,
  kContinue,
  kDelete,
  kDown,
  kHelp,
  kList,
  kListProcesses,
  kRead,
  kQuit,
  kRun,
  kSelect,
  kSet,
  kStepIn,
  kStepInst,
  kStepOut,
  kStepOver,
  kUp,
  kWrite,

  // Adding a new one?
  //   - Add in command_parser.cpp: GetVerbMap for string -> verb conversion.
  //   - Add in VerbToString for verb -> string conversion.
  kLast  // Not a real verb, keep last.
};

const char* VerbToString(Verb v);

struct Command {
  Noun noun = Noun::kNone;
  Verb verb = Verb::kNone;
  std::map<int, std::string> switches;
  std::vector<std::string> args;

  // Helper to query the switches array that returns an empty string if not
  // found.
  std::string GetSwitch(int id) const {
    auto found = switches.find(id);
    if (found == switches.end())
      return std::string();
    return found->second;
  }
};

// Switches --------------------------------------------------------------------

struct SwitchRecord {
  SwitchRecord() = default;
  SwitchRecord(int i, bool has_value, const char* n, char c = 0)
      : id(i), has_value(has_value), name(n), ch(c) {}

  const int id = 0;
  const bool has_value =
      false;  // Indicates if this switch has a value. False means it's a bool.
  const char* name =
      nullptr;  // Not including hyphens, e.g. "size" for the switch "--size".
  const char ch = 0;   // 1-character shorthand switch. 0 means no short variant.
};

// Command dispatch ------------------------------------------------------------

// Type for the callback that runs a command.
using CommandExecutor = Err (*)(Session*, const Command& cmd);

struct CommandRecord {
  CommandRecord() = default;
  CommandRecord(CommandExecutor e, const char* h) : exec(e), help(h) {}

  CommandExecutor exec = nullptr;
  const char* help = nullptr;
  std::vector<SwitchRecord> switches;  // Switches supported by this command.
};

const std::map<Noun, std::map<Verb, CommandRecord>>& GetNouns();

// Returns the possible verbs and how to execute them for the given noun.
const std::map<Verb, CommandRecord>& GetVerbsForNoun(Noun noun);

// Returns how to execute the given command. If invalid, the record will contain
// nulls.
const CommandRecord& GetRecordForCommand(const Command& cmd);

// Runs the given command.
Err DispatchCommand(Session* session, const Command& cmd);

}  // namespace zxdb

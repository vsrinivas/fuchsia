// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/zxdb/console/command.h"

#include "garnet/bin/zxdb/client/err.h"

namespace zxdb {

// These functions are implemented in the corresponding noun_* files.
std::map<Verb, CommandRecord> GetMemoryVerbs();
std::map<Verb, CommandRecord> GetProcessVerbs();
std::map<Verb, CommandRecord> GetZxdbVerbs();

const char* NounToString(Noun n) {
  switch (n) {
    case Noun::kBreakpoint:
      return "breakpoint";
    case Noun::kFrame:
      return "frame";
    case Noun::kMemory:
      return "memory";
    case Noun::kProcess:
      return "process";
    case Noun::kThread:
      return "thread";
    case Noun::kZxdb:
      return "zxdb";

    case Noun::kNone:
    default:
      return "";
  }

  static_assert(static_cast<int>(Noun::kLast) == 7,
                "Need to update NounToString for noun addition.");
}

const char* VerbToString(Verb v) {
  switch (v) {
    case Verb::kAttach:
      return "attach";
    case Verb::kBacktrace:
      return "backtrace";
    case Verb::kContinue:
      return "continue";
    case Verb::kDelete:
      return "delete";
    case Verb::kDown:
      return "down";
    case Verb::kHelp:
      return "help";
    case Verb::kList:
      return "list";
    case Verb::kRead:
      return "read";
    case Verb::kRun:
      return "run";
    case Verb::kSelect:
      return "select";
    case Verb::kSet:
      return "set";
    case Verb::kStepIn:
      return "step-in";
    case Verb::kStepInst:
      return "step-inst";
    case Verb::kStepOut:
      return "step-out";
    case Verb::kStepOver:
      return "step-over";
    case Verb::kUp:
      return "up";
    case Verb::kWrite:
      return "write";

    case Verb::kNone:
    default:
      return "";
  }

  static_assert(static_cast<int>(Verb::kLast) == 19,
                "Need to update VerbToString for noun addition.");
}

const std::map<Noun, std::map<Verb, CommandRecord>>& GetNouns() {
  static std::map<Noun, std::map<Verb, CommandRecord>> nouns;
  if (nouns.empty()) {
    nouns[Noun::kMemory] = GetMemoryVerbs();
    nouns[Noun::kProcess] = GetProcessVerbs();
    nouns[Noun::kZxdb] = GetZxdbVerbs();
  }
  return nouns;
}

const std::map<Verb, CommandRecord>& GetVerbsForNoun(Noun noun) {
  const auto& nouns = GetNouns();
  const auto found = nouns.find(noun);
  if (found == nouns.end()) {
    static std::map<Verb, CommandRecord> empty_map;
    return empty_map;
  }
  return found->second;
}

const CommandRecord& GetRecordForCommand(const Command& cmd) {
  const auto& verbs = GetVerbsForNoun(cmd.noun);
  const auto found = verbs.find(cmd.verb);
  if (found == verbs.end()) {
    static CommandRecord empty_command_record;
    return empty_command_record;
  }
  return found->second;
}

Err DispatchCommand(Session* session, const Command& cmd, OutputBuffer* out) {
  const CommandRecord& record = GetRecordForCommand(cmd);
  if (!record.exec) {
    return Err(ErrType::kInput,
               "Invalid command \"" + std::string(NounToString(cmd.noun)) + " " +
               VerbToString(cmd.verb) + "\".");
  }
  return record.exec(session, cmd, out);
}

}  // namespace zxdb

// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <map>
#include <vector>

#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/console/command_group.h"
#include "src/developer/debug/zxdb/console/switch_record.h"

namespace zxdb {

class Command;
class Console;
class ConsoleContext;
class Thread;

enum class Noun {
  kNone = 0,

  kBreakpoint,
  kFrame,
  kJob,
  kProcess,
  kGlobal,
  kSymServer,
  kThread,

  // Adding a new one? Add to GetNouns().
  kLast  // Not a real noun, keep last.
};

struct NounRecord {
  NounRecord();
  NounRecord(std::initializer_list<std::string> aliases, const char* short_help,
             const char* help, CommandGroup command_group);
  ~NounRecord();

  // These are the user-typed strings that will name this noun. The [0]th one
  // is the canonical name.
  std::vector<std::string> aliases;

  const char* short_help = nullptr;  // One-line help.
  const char* help = nullptr;

  // What logical place this command should appear in the help under, in
  // addition to the "nouns" list. This could be none if this noun should only
  // appear in the nouns list.
  CommandGroup command_group;
};

// Returns all known nouns. The contents of this map will never change once
// it is called.
const std::map<Noun, NounRecord>& GetNouns();

// Converts the given noun to the canonical name.
std::string NounToString(Noun n);

// Returns the mapping from possible inputs to the noun. This is an inverted
// version of the map returned by GetNouns().
const std::map<std::string, Noun>& GetStringNounMap();

// Handles execution of command input consisting of a noun and no verb.
// For example "process", "process 2 thread", "thread 5".
Err ExecuteNoun(ConsoleContext* context, const Command& cmd);

// Populates the nouns map.
void AppendNouns(std::map<Noun, NounRecord>* nouns);

// Returns the set of all switches valid for nouns. Since a command can have
// multiple nouns, which set of switches apply can be complicated.
//
// Currently, when a command lacks a verb, the logic in ExecuteNoun() will
// prioritize which one the user meant and therefore, which one the switches
// will apply to.
//
// If the noun switches start getting more complicated, we will probably want
// to have a priority associated with a noun so the parser can figure out
// which noun is being executed and apply switches on a per-noun basis.
const std::vector<SwitchRecord>& GetNounSwitches();

}  // namespace zxdb
